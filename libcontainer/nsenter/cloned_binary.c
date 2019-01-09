#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/syscall.h>

#include <linux/magic.h>
#include <linux/memfd.h>

#define MEMFD_COMMENT "runc_cloned:/proc/self/exe"
#define MEMFD_LNKNAME "/memfd:" MEMFD_COMMENT " (deleted)"

/* Use our own wrapper for memfd_create. */
#if !defined(SYS_memfd_create) && defined(__NR_memfd_create)
#  define SYS_memfd_create __NR_memfd_create
#endif
#ifndef SYS_memfd_create
#  error "memfd_create(2) syscall not supported by this glibc version"
#endif
int memfd_create(const char *name, unsigned int flags)
{
	return syscall(SYS_memfd_create, name, flags);
}

/* This comes directly from <linux/fcntl.h>. */
#ifndef F_LINUX_SPECIFIC_BASE
# define F_LINUX_SPECIFIC_BASE 1024
#endif
#ifndef F_ADD_SEALS
# define F_ADD_SEALS (F_LINUX_SPECIFIC_BASE + 9)
# define F_GET_SEALS (F_LINUX_SPECIFIC_BASE + 10)
#endif
#ifndef F_SEAL_SEAL
# define F_SEAL_SEAL   0x0001	/* prevent further seals from being set */
# define F_SEAL_SHRINK 0x0002	/* prevent file from shrinking */
# define F_SEAL_GROW   0x0004	/* prevent file from growing */
# define F_SEAL_WRITE  0x0008	/* prevent writes */
#endif

/*
 * Verify whether we are currently in a self-cloned program. It's not really
 * possible to trivially identify a memfd compared to a regular tmpfs file, so
 * the best we can do is to check whether the readlink(2) looks okay and that
 * it is on a tmpfs.
 */
static int is_self_cloned(void)
{
	struct statfs statfsbuf = {0};
	char linkname[PATH_MAX + 1] = {0};

	if (statfs("/proc/self/exe", &statfsbuf) < 0)
		return -1;
	if (readlink("/proc/self/exe", linkname, PATH_MAX) < 0)
		return -1;

	return statfsbuf.f_type == TMPFS_MAGIC &&
		!strncmp(linkname, MEMFD_LNKNAME, PATH_MAX);
}

/*
 * Basic wrapper around mmap(2) that gives you the file length so you can
 * safely treat it as an ordinary buffer. Only gives you read access.
 */
static char *read_file(char *path, size_t *length)
{
	int fd;
	char buf[4096], *copy = NULL;

	if (!length)
		goto err;
	*length = 0;

	fd = open(path, O_RDONLY|O_CLOEXEC);
	if (fd < 0)
		goto err_free;

	for (;;) {
		int n;
		char *old = copy;

		n = read(fd, buf, sizeof(buf));
		if (n < 0)
			goto err_fd;
		if (!n)
			break;

		do {
			copy = realloc(old, (*length + n) * sizeof(*old));
		} while(!copy);

		memcpy(copy + *length, buf, n);
		*length += n;
	}
	close(fd);
	return copy;

err_fd:
	close(fd);
err_free:
	free(copy);
err:
	return NULL;
}

/*
 * A poor-man's version of "xargs -0". Basically parses a given block of
 * NUL-delimited data, within the given length and adds a pointer to each entry
 * to the array of pointers.
 */
static int parse_xargs(char *data, int data_length, char ***output)
{
	int num = 0;
	char *cur = data;

	if (!data || *output)
		return -1;

	do {
		*output = malloc(sizeof(**output));
	} while (!*output);

	while (cur < data + data_length) {
		char **old = *output;

		num++;
		do {
			*output = realloc(old, (num + 1) * sizeof(*old));
		} while (!*output);

		(*output)[num - 1] = cur;
		cur += strlen(cur) + 1;
	}
	(*output)[num] = NULL;
	return num;
}

/*
 * "Parse" out argv and envp from /proc/self/cmdline and /proc/self/environ.
 * This is necessary because we are running in a context where we don't have a
 * main() that we can just get the arguments from.
 */
static int fetchve(char ***argv, char ***envp)
{
	char *cmdline, *environ;
	size_t cmdline_size, environ_size;

	cmdline = read_file("/proc/self/cmdline", &cmdline_size);
	if (!cmdline)
		goto err;
	environ = read_file("/proc/self/environ", &environ_size);
	if (!environ)
		goto err_free;

	if (parse_xargs(cmdline, cmdline_size, argv) <= 0)
		goto err_free_both;
	if (parse_xargs(environ, environ_size, envp) <= 0)
		goto err_free_both;

	return 0;

err_free_both:
	free(environ);
err_free:
	free(cmdline);
err:
	return -1;
}

static int clone_binary(void)
{
	int binfd, memfd, err;
	ssize_t sent = 0;
	struct stat statbuf = {0};

	binfd = open("/proc/self/exe", O_RDONLY|O_CLOEXEC);
	if (binfd < 0)
		goto err;
	if (fstat(binfd, &statbuf) < 0)
		goto err_binfd;

	memfd = memfd_create(MEMFD_COMMENT, MFD_CLOEXEC|MFD_ALLOW_SEALING);
	if (memfd < 0)
		goto err_binfd;

	while (sent < statbuf.st_size) {
		ssize_t n = sendfile(memfd, binfd, NULL, statbuf.st_size - sent);
		if (n < 0)
			goto err_memfd;
		sent += n;
	}

	err = fcntl(memfd, F_ADD_SEALS, F_SEAL_SHRINK|F_SEAL_GROW|F_SEAL_WRITE|F_SEAL_SEAL);
	if (err < 0)
		goto err_memfd;

	close(binfd);
	return memfd;

err_memfd:
	close(memfd);
err_binfd:
	close(binfd);
err:
	return -1;
}

int ensure_cloned_binary(void)
{
	int execfd;
	char **argv = NULL, **envp = NULL;

	/* Check that we're not self-cloned, and if we are then bail. */
	int cloned = is_self_cloned();
	if (cloned != 0)
		return cloned;

	if (fetchve(&argv, &envp) < 0)
		return -1;

	execfd = clone_binary();
	if (execfd < 0)
		return -1;

	fexecve(execfd, argv, envp);
	return -1;
}
