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

static int state_rename_visible;
static int snapshot_directory_rename_visible;

int fsync(int fd)
{
	static fsync_fn real_fsync;
	static int directory_failure_done;
	static int state_directory_failure_done;
	const char *target = getenv("UDB_SNAPSHOT_FSYNC_FAIL_TARGET");
	const char *directory_target = getenv("UDB_SNAPSHOT_DIR_FSYNC_FAIL_TARGET");
	const char *directory_snapshot = getenv("UDB_SNAPSHOT_DIR_FSYNC_FAIL_SNAPSHOT");
	const char *state_directory_target = getenv("UDB_STATE_DIR_FSYNC_FAIL_TARGET");
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
	if (directory_target && ((directory_snapshot && snapshot_directory_rename_visible) ||
						 (!directory_snapshot && !directory_failure_done)) &&
	    snprintf(fd_path, sizeof(fd_path), "/proc/self/fd/%d", fd) < (int)sizeof(fd_path) &&
	    (path_len = readlink(fd_path, path, sizeof(path) - 1)) >= 0)
	{
		path[path_len] = '\0';
		if (!strcmp(path, directory_target))
		{
			if (directory_snapshot)
				snapshot_directory_rename_visible = 0;
			else
				directory_failure_done = 1;
			fprintf(stderr, "UDB_TEST_SNAPSHOT_DIR_FSYNC_FAIL: %s\n", path);
			errno = EIO;
			return -1;
		}
	}
	if (state_directory_target && state_rename_visible && !state_directory_failure_done &&
		snprintf(fd_path, sizeof(fd_path), "/proc/self/fd/%d", fd) < (int)sizeof(fd_path) &&
		(path_len = readlink(fd_path, path, sizeof(path) - 1)) >= 0)
	{
		path[path_len] = '\0';
		if (!strcmp(path, state_directory_target))
		{
			state_directory_failure_done = 1;
			fprintf(stderr, "UDB_TEST_STATE_DIR_FSYNC_FAIL: %s\n", path);
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
	const char *state_directory_target = getenv("UDB_STATE_DIR_FSYNC_FAIL_TARGET");
	const char *directory_snapshot = getenv("UDB_SNAPSHOT_DIR_FSYNC_FAIL_SNAPSHOT");
	char temporary[1024];
	int result;

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
	result = real_rename(oldpath, newpath);
	if (!result && state_directory_target && newpath && strstr(newpath, "/.udb_state") &&
		!strcmp(newpath + strlen(newpath) - strlen("/.udb_state"), "/.udb_state"))
		state_rename_visible = 1;
	if (!result && directory_snapshot && newpath && !strcmp(newpath, directory_snapshot))
		snapshot_directory_rename_visible = 1;
	return result;
}
