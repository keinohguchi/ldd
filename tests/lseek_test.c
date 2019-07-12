/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "kselftest.h"

struct test {
	const char	*const name;
	const char	*const dev;
	const size_t	alloc;
	const char	*const data;
	const size_t	len;
};

static void test(const struct test *restrict t)
{
	char path[PATH_MAX];
	char buf[LINE_MAX];
	int ret, fd;
	long val;
	FILE *fp;

	ret = snprintf(path, sizeof(path), "/sys/devices/%s/alloc", t->dev);
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
	val = strtol(buf, NULL, 10);
	if (val != t->alloc) {
		fprintf(stderr, "%s: unexpected alloc value:\n\t- want: %ld\n\t-  got: %ld\n",
			t->name, t->alloc, val);
		goto err;
	}
	ret = snprintf(path, sizeof(path), "/dev/%s", t->dev);
	if (ret < 0)
		goto perr;
	fd = open(path, O_WRONLY|O_TRUNC);
	if (fd == -1)
		goto perr;
	ret = write(fd, t->data, t->len);
	if (ret == -1)
		goto perr;
	if (ret != t->len) {
		fprintf(stderr, "%s: unexpected write length:\n\t- want: %ld\n\t-  got: %d\n",
			t->name, t->len, ret);
	}
	if (close(fd) == -1)
		goto perr;
	ret = snprintf(path, sizeof(path), "/sys/devices/%s/len", t->dev);
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
	val = strtol(buf, NULL, 10);
	if (val != t->len) {
		fprintf(stderr, "%s: unexpected write length:\n\t- want: %ld\n-  got: %ld\n",
			t->name, t->len, val);
		goto err;
	}
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
			.name	= "lseek16",
			.dev	= "lseek16",
			.alloc	= 16,
			.data	= "0123456789",
			.len	= 10,
		},
		{
			.name	= "lseek64",
			.dev	= "lseek64",
			.alloc	= 64,
			.data	= "0123456789",
			.len	= 10,
		},
		{
			.name	= "lseek128",
			.dev	= "lseek128",
			.alloc	= 128,
			.data	= "0123456789",
			.len	= 10,
		},
		{
			.name	= "lseek256",
			.dev	= "lseek256",
			.alloc	= 256,
			.data	= "0123456789",
			.len	= 10,
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
