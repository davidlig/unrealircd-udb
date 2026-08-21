/* Test-only LD_PRELOAD fixture for configured UDB snapshot persistence failures. */
#define _GNU_SOURCE

#include <dlfcn.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int (*rename_fn)(const char *oldpath, const char *newpath);
typedef int (*fsync_fn)(int fd);

int fsync(int fd)
{
	static fsync_fn real_fsync;
	const char *target = getenv("UDB_SNAPSHOT_FSYNC_FAIL_TARGET");
	char fd_path[64];
	char path[1024];
	ssize_t path_len;

	if (!real_fsync)
		real_fsync = (fsync_fn)dlsym(RTLD_NEXT, "fsync");
	if (target && snprintf(fd_path, sizeof(fd_path), "/proc/self/fd/%d", fd) < (int)sizeof(fd_path) &&
	    (path_len = readlink(fd_path, path, sizeof(path) - 1)) >= 0)
	{
		path[path_len] = '\0';
		if (!strcmp(path, target))
		{
			fprintf(stderr, "UDB_TEST_SNAPSHOT_FSYNC_FAIL: %s\n", path);
			errno = EIO;
			return -1;
		}
	}
	return real_fsync(fd);
}

int rename(const char *oldpath, const char *newpath)
{
	static rename_fn real_rename;
	const char *target = getenv("UDB_SNAPSHOT_RENAME_FAIL_TARGET");
	const char *arm = getenv("UDB_SNAPSHOT_RENAME_FAIL_ARM");
	char temporary[1024];

	if (!real_rename)
		real_rename = (rename_fn)dlsym(RTLD_NEXT, "rename");
	if (target && (!arm || !access(arm, F_OK)) && oldpath && newpath &&
	    snprintf(temporary, sizeof(temporary), "%s.tmp", target) < (int)sizeof(temporary) &&
	    !strcmp(oldpath, temporary) && !strcmp(newpath, target))
	{
		fprintf(stderr, "UDB_TEST_SNAPSHOT_RENAME_FAIL: %s -> %s\n", oldpath, newpath);
		errno = EIO;
		return -1;
	}
	return real_rename(oldpath, newpath);
}
