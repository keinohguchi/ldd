/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "kselftest.h"

struct test {
	const char	*const name;
	const char	*const dev;
	unsigned int	readers;
	unsigned int	writers;
};

struct context {
	const struct test	*const t;
	pthread_mutex_t	lock;
	pthread_cond_t	cond;
	int		start;
};

static void *tester(struct context *ctx, int flags)
{
	const struct test *const t = ctx->t;
	char path[PATH_MAX];
	char buf[BUFSIZ];
	FILE *fp;
	int err, fd;
	long val;

	/* wait for the start */
	pthread_mutex_lock(&ctx->lock);
	while (!ctx->start)
		pthread_cond_wait(&ctx->cond, &ctx->lock);
	pthread_mutex_unlock(&ctx->lock);

	err = snprintf(path, sizeof(path), "/sys/module/sem/parameters/default_sem_count");
	if (err < 0)
		goto perr;
	fp = fopen(path, "r");
	if (fp == NULL)
		goto perr;
	fread(buf, sizeof(buf), 1, fp);
	if (ferror(fp))
		goto perr;
	val = strtol(buf, NULL, 10);
	if (val != 1) {
		fprintf(stderr, "%s: unexpected default semaphore count:\n\t- want: 1\n\t-  got: %ld\n",
			t->name, val);
		goto err;
	}
	if (fclose(fp))
		goto perr;
	err = snprintf(path, sizeof(path), "/dev/%s", t->dev);
	if (err < 0)
		goto perr;
	fd = open(path, flags);
	if (fd == -1)
		goto perr;
	if (close(fd))
		goto perr;
	return (void *)EXIT_SUCCESS;
perr:
	fprintf(stderr, "%s: %s\n", t->name, strerror(errno));
err:
	return (void *)EXIT_FAILURE;
}

static void *reader(void *ctx)
{
	return tester(ctx, O_RDONLY);
}

static void *writer(void *ctx)
{
	return tester(ctx, O_WRONLY);
}

static void test(const struct test *restrict t)
{
	struct context ctx = {
		.t	= t,
		.lock	= PTHREAD_MUTEX_INITIALIZER,
		.cond	= PTHREAD_COND_INITIALIZER,
		.start	= 0,
	};
	pthread_t rids[t->readers];
	pthread_t wids[t->writers];
	char buf[LINE_MAX], path[PATH_MAX];
	int fail = 0;
	int i, err;
	FILE *fp;
	long val;

	err = snprintf(path, sizeof(path), "/sys/class/misc/%s/lockers", t->dev);
	if (err < 0)
		goto perr;
	fp = fopen(path, "r");
	if (!fp)
		goto perr;
	err = fread(buf, sizeof(buf), 1, fp);
	if (err == 0 && ferror(fp))
		goto perr;
	if (fclose(fp))
		goto perr;
	val = strtol(buf, NULL, 10);
	if (val != 0) {
		fprintf(stderr, "%s: unexpected beginning lock count:\n\t- want: 0\n\t-  got: %ld\n",
			t->name, val);
		goto err;
	}
	memset(rids, 0, sizeof(rids));
	memset(wids, 0, sizeof(wids));
	for (i = 0; i < t->readers; i++) {
		err = pthread_create(&rids[i], NULL, reader, &ctx);
		if (err) {
			fprintf(stderr, "%s: %s\n", t->name, strerror(err));
			fail++;
			goto join;
		}
	}
	for (i = 0; i < t->writers; i++) {
		err = pthread_create(&wids[i], NULL, writer, &ctx);
		if (err) {
			fprintf(stderr, "%s: %s\n", t->name, strerror(err));
			fail++;
			goto join;
		}
	}
	err = pthread_mutex_lock(&ctx.lock);
	if (err) {
		errno = err;
		goto perr;
	}
	ctx.start = 1;
	err = pthread_mutex_unlock(&ctx.lock);
	if (err) {
		errno = err;
		goto perr;
	}
	err = pthread_cond_broadcast(&ctx.cond);
	if (err) {
		errno = err;
		goto perr;
	}
	err = pthread_yield();
	if (err) {
		errno = err;
		goto perr;
	}
	err = snprintf(path, sizeof(path), "/sys/class/misc/%s/lockers", t->dev);
	if (err < 0)
		goto perr;
	fp = fopen(path, "r");
	if (fp == NULL)
		goto perr;
	fread(buf, sizeof(buf), 1, fp);
	if (ferror(fp))
		goto perr;
	if (fclose(fp))
		goto perr;
	val = strtol(buf, NULL, 10);
	fprintf(stdout, "%s:\n\tlockers: %ld\n", t->name, val);
	fflush(stdout);
join:
	for (i = 0; i < t->readers; i++) {
		void *retp = NULL;
		if (!rids[i])
			continue;
		err = pthread_join(rids[i], &retp);
		if (err) {
			fprintf(stderr, "%s: %s\n", t->name, strerror(err));
			fail++;
		}
		if (retp != (void *)EXIT_SUCCESS)
			fail++;
	}
	for (i = 0; i < t->writers; i++) {
		void *retp = NULL;
		if (!wids[i])
			continue;
		err = pthread_join(wids[i], &retp);
		if (err) {
			fprintf(stderr, "%s: %s\n", t->name, strerror(err));
			fail++;
		}
		if (retp != (int *)EXIT_SUCCESS)
			fail++;
	}
	err = snprintf(path, sizeof(path), "/sys/class/misc/%s/lockers", t->dev);
	if (err < 0)
		goto perr;
	fp = fopen(path, "r");
	if (!fp)
		goto perr;
	err = fread(buf, sizeof(buf), 1, fp);
	if (err == 0 && ferror(fp))
		goto perr;
	if (fclose(fp))
		goto perr;
	val = strtol(buf, NULL, 10);
	if (val != 0) {
		fprintf(stderr, "%s: unexpected ending lock count:\n\t- want: 0\n\t-  got: %ld\n",
			t->name, val);
		goto err;
	}
	if (fail)
		goto err;
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
			.name		= "one reader",
			.dev		= "sem0",
			.readers	= 1,
			.writers	= 0,
		},
		{
			.name		= "one writer",
			.dev		= "sem0",
			.readers	= 0,
			.writers	= 1,
		},
		{
			.name		= "one reader and one writer",
			.dev		= "sem0",
			.readers	= 1,
			.writers	= 1,
		},
		{
			.name		= "16 readers",
			.dev		= "sem0",
			.readers	= 16,
			.writers	= 0,
		},
		{
			.name		= "16 writers",
			.dev		= "sem0",
			.readers	= 0,
			.writers	= 16,
		},
		{
			.name		= "16 readers and 16 writers",
			.dev		= "sem0",
			.readers	= 16,
			.writers	= 16,
		},
		{
			.name		= "32 readers",
			.dev		= "sem0",
			.readers	= 32,
			.writers	= 0,
		},
		{
			.name		= "32 writers",
			.dev		= "sem0",
			.readers	= 0,
			.writers	= 32,
		},
		{
			.name		= "32 readers and 32 writers",
			.dev		= "sem0",
			.readers	= 32,
			.writers	= 32,
		},
		{
			.name		= "64 readers",
			.dev		= "sem0",
			.readers	= 64,
			.writers	= 0,
		},
		{
			.name		= "64 writers",
			.dev		= "sem0",
			.readers	= 0,
			.writers	= 64,
		},
		{
			.name		= "64 readers and 64 writers",
			.dev		= "sem0",
			.readers	= 64,
			.writers	= 64,
		},
		{
			.name		= "256 readers and 16 writers",
			.dev		= "sem0",
			.readers	= 256,
			.writers	= 16,
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
			fprintf(stderr, "%s: %s\n",
				t->name, strsignal(WTERMSIG(status)));
			goto err;
		}
		if (!WIFEXITED(status)) {
			fprintf(stderr, "%s: returns failed exit\n", t->name);
			goto err;
		}
		ksft_inc_pass_cnt();
		continue;
perr:
		fprintf(stderr, "%s: %s\n", t->name, strerror(errno));
err:
		ksft_inc_fail_cnt();
	}
	if (ksft_get_fail_cnt())
		ksft_exit_fail();
	ksft_exit_pass();
}
