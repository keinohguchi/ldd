/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <sched.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include "kselftest.h"

struct test {
	const char	*const name;
	const char	*const dev;
	unsigned int	readers;
	unsigned int	writers;
};

struct context {
	const struct test	*const t;
	pthread_mutex_t		lock;
	pthread_cond_t		cond;
	int			start;
};

static void *tester(struct context *ctx, int flags)
{
	const struct test *const t = ctx->t;
	char path[PATH_MAX];
	int ret, fd;

	pthread_mutex_lock(&ctx->lock);
	while (!ctx->start)
		pthread_cond_wait(&ctx->cond, &ctx->lock);
	pthread_mutex_unlock(&ctx->lock);

	ret = snprintf(path, sizeof(path), "/dev/%s", t->dev);
	if (ret < 0)
		goto perr;
	fd = open(path, flags);
	if (fd == -1)
		goto perr;
	pthread_yield();
	if (close(fd) == -1)
		goto perr;
	pthread_exit((void *)EXIT_SUCCESS);
perr:
	perror(t->name);
	pthread_exit((void *)EXIT_FAILURE);
}

static void *reader(void *arg)
{
	return tester(arg, O_RDONLY);
}

static void *writer(void *arg)
{
	return tester(arg, O_WRONLY);
}

static void test(const struct test *restrict t)
{
	unsigned int nr = (t->readers+t->writers)*2;
	const struct rlimit limit = {
		.rlim_cur	= nr > 1024 ? nr : 1024,
		.rlim_max	= nr > 1024 ? nr : 1024,
	};
	struct context ctx = {
		.t	= t,
		.lock	= PTHREAD_MUTEX_INITIALIZER,
		.cond	= PTHREAD_COND_INITIALIZER,
		.start	= 0,
	};
	pthread_t readers[t->readers], writers[t->writers];
	char buf[BUFSIZ], path[PATH_MAX];
	cpu_set_t cpus;
	int i, ret, err;
	FILE *fp;
	long got;

	ret = snprintf(path, sizeof(path), "/sys/class/misc/%s/active", t->dev);
	if (ret < 0)
		goto perr;
	fp = fopen(path, "r");
	if (!fp)
		goto perr;
	ret = fread(buf, sizeof(buf), 1, fp);
	if (ret == 0 && ferror(fp))
		goto perr;
	if (fclose(fp) == -1)
		goto perr;
	got = strtol(buf, NULL, 10);
	if (got != 0) {
		fprintf(stderr, "%s: unexpected initial active context:\n\t- want: 0\n\t-  got: %ld\n",
			t->name, got);
		goto err;
	}
	ret = setrlimit(RLIMIT_NOFILE, &limit);
	if (ret == -1)
		goto perr;
	CPU_ZERO(&cpus);
	ret = sched_getaffinity(0, sizeof(cpus), &cpus);
	if (ret == -1)
		goto perr;
	nr = CPU_COUNT(&cpus);
	memset(readers, 0, sizeof(readers));
	memset(writers, 0, sizeof(writers));
	for (i = 0; i < t->readers; i++) {
		pthread_attr_t attr;

		memset(&attr, 0, sizeof(attr));
		CPU_ZERO(&cpus);
		CPU_SET(i%nr, &cpus);
		err = pthread_attr_setaffinity_np(&attr, sizeof(cpus), &cpus);
		if (err) {
			errno = err;
			goto perr;
		}
		err = pthread_create(&readers[i], &attr, reader, &ctx);
		if (err) {
			errno = err;
			goto perr;
		}
	}
	for (i = 0; i < t->writers; i++) {
		pthread_attr_t attr;

		memset(&attr, 0, sizeof(attr));
		CPU_ZERO(&cpus);
		CPU_SET(i%nr, &cpus);
		err = pthread_attr_setaffinity_np(&attr, sizeof(cpus), &cpus);
		if (err) {
			errno = err;
			goto perr;
		}
		err = pthread_create(&writers[i], &attr, writer, &ctx);
		if (err) {
			errno = err;
			goto perr;
		}
	}
	err = pthread_mutex_lock(&ctx.lock);
	if (err) {
		errno = err;
		goto perr;
	}
	ctx.start = 1;
	err = pthread_cond_broadcast(&ctx.cond);
	if (err) {
		errno = err;
		goto perr;
	}
	err = pthread_mutex_unlock(&ctx.lock);
	if (err) {
		errno = err;
		goto perr;
	}
	for (i = 0; i < t->readers; i++) {
		void *retp;
		if (!readers[i])
			continue;
		err = pthread_join(readers[i], &retp);
		if (err) {
			errno = err;
			goto perr;
		}
		if (retp != (void *)EXIT_SUCCESS)
			goto err;
	}
	for (i = 0; i < t->writers; i++) {
		void *retp;
		if (!writers[i])
			continue;
		err = pthread_join(writers[i], &retp);
		if (err) {
			errno = err;
			goto perr;
		}
		if (retp != (void *)EXIT_SUCCESS)
			goto err;
	}
	ret = snprintf(path, sizeof(path), "/sys/class/misc/%s/active", t->dev);
	if (ret < 0)
		goto perr;
	fp = fopen(path, "r");
	if (!fp)
		goto perr;
	ret = fread(buf, sizeof(buf), 1, fp);
	if (ret == 0 && ferror(fp))
		goto perr;
	if (fclose(fp) == -1)
		goto perr;
	got = strtol(buf, NULL, 10);
	if (got != 0) {
		fprintf(stderr, "%s: unexpected final active context:\n\t- want: 0\n\t-  got: %ld\n",
			t->name, got);
		goto err;
	}
	ret = snprintf(path, sizeof(path), "/sys/class/misc/%s/free", t->dev);
	if (ret < 0)
		goto perr;
	fp = fopen(path, "r");
	if (!fp)
		goto perr;
	ret = fread(buf, sizeof(buf), 1, fp);
	if (ret == 0 && ferror(fp))
		goto perr;
	if (fclose(fp) == -1)
		goto perr;
	got = strtol(buf, NULL, 10);
	printf("%41s: %3ld context(s) on free list\n", t->name, got);
	exit(EXIT_SUCCESS);
perr:
	perror(t->name);
err:
	exit(EXIT_FAILURE);
}

int main(void)
{
	const struct test *t, tests[] = {
		{
			.name		= "1 reader on seqlock0",
			.dev		= "seqlock0",
			.readers	= 1,
			.writers	= 0,
		},
		{
			.name		= "1 writer on seqlock1",
			.dev		= "seqlock1",
			.readers	= 0,
			.writers	= 1,
		},
		{
			.name		= "1 reader and 1 writer on seqlock0",
			.dev		= "seqlock0",
			.readers	= 1,
			.writers	= 1,
		},
		{
			.name		= "32 readers on seqlock0",
			.dev		= "seqlock0",
			.readers	= 32,
			.writers	= 0,
		},
		{
			.name		= "32 writers on seqlock1",
			.dev		= "seqlock1",
			.readers	= 0,
			.writers	= 32,
		},
		{
			.name		= "32 readers and 32 writers on seqlock0",
			.dev		= "seqlock0",
			.readers	= 32,
			.writers	= 32,
		},
		{
			.name		= "64 readers on seqlock1",
			.dev		= "seqlock1",
			.readers	= 64,
			.writers	= 0,
		},
		{
			.name		= "64 writers on seqlock0",
			.dev		= "seqlock0",
			.readers	= 0,
			.writers	= 64,
		},
		{
			.name		= "64 readers and 64 writers on seqlock1",
			.dev		= "seqlock1",
			.readers	= 64,
			.writers	= 64,
		},
		{
			.name		= "256 readers and 16 writers on seqlock0",
			.dev		= "seqlock0",
			.readers	= 256,
			.writers	= 16,
		},
		{
			.name		= "1024 readers and 256 writers on seqlock1",
			.dev		= "seqlock1",
			.readers	= 1024,
			.writers	= 256,
		},
		{
			.name		= "2048 readers and 512 writers on seqlock0",
			.dev		= "seqlock0",
			.readers	= 2048,
			.writers	= 512,
		},
		{
			.name		= "2048 readers and 1024 writers on seqlock1",
			.dev		= "seqlock1",
			.readers	= 2048,
			.writers	= 1024,
		},
		{.name = NULL},
	};

	for (t = tests; t->name; t++) {
		int ret, status;
		pid_t pid;

		pid = fork();
		if (pid == -1)
			goto perr;
		else if (pid == 0)
			test(t);

		ret = waitpid(pid, &status, 0);
		if (ret == -1)
			goto perr;
		if (WIFSIGNALED(status)) {
			fprintf(stderr, "%s: signaled with %s\n",
				t->name, strsignal(WTERMSIG(status)));
			goto err;
		}
		if (!WIFEXITED(status)) {
			fprintf(stderr, "%s: does not exit\n",
				t->name);
			goto err;
		}
		if (WEXITSTATUS(status))
		    goto err;
		ksft_inc_pass_cnt();
		continue;
perr:
		perror(t->name);
err:
		ksft_inc_fail_cnt();
	}
	if (ksft_get_fail_cnt())
		ksft_exit_fail();
	ksft_exit_pass();
}
