/* Test-only LD_PRELOAD fixture for a single configured UDB snapshot rename. */
#define _GNU_SOURCE

#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int (*rename_fn)(const char *oldpath, const char *newpath);

int rename(const char *oldpath, const char *newpath)
{
	static rename_fn real_rename;
	const char *target = getenv("UDB_SNAPSHOT_RENAME_FAIL_TARGET");
	char temporary[1024];

	if (!real_rename)
		real_rename = (rename_fn)dlsym(RTLD_NEXT, "rename");
	if (target && oldpath && newpath &&
	    snprintf(temporary, sizeof(temporary), "%s.tmp", target) < (int)sizeof(temporary) &&
	    !strcmp(oldpath, temporary) && !strcmp(newpath, target))
	{
		fprintf(stderr, "UDB_TEST_SNAPSHOT_RENAME_FAIL: %s -> %s\n", oldpath, newpath);
		errno = EIO;
		return -1;
	}
	return real_rename(oldpath, newpath);
}
