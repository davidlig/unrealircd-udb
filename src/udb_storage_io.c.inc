/*
 * UDB 4 native storage: exclusive, non-destructive 64-bit file operations.
 * File creation is provisioning, not a transaction commit. The engine must
 * validate a complete manifest and its durable journal before publication.
 */
#ifndef _WIN32
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
typedef char UdbStorageRequires64BitOffsets[(sizeof(off_t) >= 8) ? 1 : -1];
#endif

static int udb_storage_mutex_init(UdbStorageMutex *mutex)
{
	if (!mutex)
		return 0;
	memset(mutex, 0, sizeof(*mutex));
#ifdef _WIN32
	if (!InitializeCriticalSectionEx(&mutex->native, 0, 0))
		return 0;
#else
	if (pthread_mutex_init(&mutex->native, NULL))
		return 0;
#endif
	mutex->initialized = 1;
	return 1;
}

static void udb_storage_mutex_lock(UdbStorageMutex *mutex)
{
#ifdef _WIN32
	EnterCriticalSection(&mutex->native);
#else
	if (pthread_mutex_lock(&mutex->native))
		abort();
#endif
}

static void udb_storage_mutex_unlock(UdbStorageMutex *mutex)
{
#ifdef _WIN32
	LeaveCriticalSection(&mutex->native);
#else
	if (pthread_mutex_unlock(&mutex->native))
		abort();
#endif
}

static void udb_storage_mutex_destroy(UdbStorageMutex *mutex)
{
	if (!mutex || !mutex->initialized)
		return;
#ifdef _WIN32
	DeleteCriticalSection(&mutex->native);
#else
	if (pthread_mutex_destroy(&mutex->native))
		abort();
#endif
	mutex->initialized = 0;
}

static int udb_storage_file_name_valid(const char *name)
{
	size_t length;

	if (!name || (strncmp(name, "udb_", 4) && strncmp(name, "stage_", 6)))
		return 0;
	length = strlen(name);
	if (length < 5 || length > 63 || strstr(name, "..") || name[length - 1] == '.')
		return 0;
	for (size_t i = 0; i < length; i++)
	{
		unsigned char c = (unsigned char)name[i];

		if (!(c >= 'A' && c <= 'Z') && !(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '_' && c != '-' &&
			c != '.')
			return 0;
	}
	return 1;
}

static int udb_storage_io_range_valid(uint64_t offset, size_t length)
{
	return offset <= INT64_MAX && (uint64_t)length <= (uint64_t)INT64_MAX - offset;
}

#ifdef _WIN32
/* Stack-bounded owner-only security descriptor; Windows copies it on create. */
typedef struct UdbStorageWinSecurity
{
	union
	{
		TOKEN_USER align;
		unsigned char bytes[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
	} user;
	union
	{
		ACL align;
		unsigned char bytes[256];
	} acl;
	SECURITY_DESCRIPTOR descriptor;
	SECURITY_ATTRIBUTES attributes;
} UdbStorageWinSecurity;

static int udb_storage_win_security_init(UdbStorageWinSecurity *security)
{
	HANDLE token;
	DWORD length = 0;
	DWORD acl_length;
	PSID sid;

	memset(security, 0, sizeof(*security));
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
		return 0;
	if (!GetTokenInformation(token, TokenUser, security->user.bytes, sizeof(security->user.bytes), &length))
	{
		CloseHandle(token);
		return 0;
	}
	if (!CloseHandle(token))
		return 0;
	sid = security->user.align.User.Sid;
	if (!IsValidSid(sid))
		return 0;
	acl_length = sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD) + GetLengthSid(sid);
	if (acl_length > sizeof(security->acl.bytes) ||
		!InitializeAcl((PACL)security->acl.bytes, acl_length, ACL_REVISION) ||
		!AddAccessAllowedAce((PACL)security->acl.bytes, ACL_REVISION, FILE_ALL_ACCESS, sid) ||
		!InitializeSecurityDescriptor(&security->descriptor, SECURITY_DESCRIPTOR_REVISION) ||
		!SetSecurityDescriptorOwner(&security->descriptor, sid, FALSE) ||
		!SetSecurityDescriptorDacl(&security->descriptor, TRUE, (PACL)security->acl.bytes, FALSE) ||
		!SetSecurityDescriptorControl(&security->descriptor, SE_DACL_PROTECTED, SE_DACL_PROTECTED))
		return 0;
	security->attributes.nLength = sizeof(security->attributes);
	security->attributes.lpSecurityDescriptor = &security->descriptor;
	security->attributes.bInheritHandle = FALSE;
	return 1;
}

static int udb_storage_win_owner_only(HANDLE handle, UdbStorageBudget *budget, UdbStorageWinSecurity *security)
{
	DWORD length = 0;
	PSECURITY_DESCRIPTOR descriptor = NULL;
	PSID owner = NULL;
	PACL acl = NULL;
	BOOL defaulted, present;
	SECURITY_DESCRIPTOR_CONTROL control;
	DWORD revision;
	PACCESS_ALLOWED_ACE ace;
	int valid = 0;

	GetKernelObjectSecurity(handle, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, NULL, 0, &length);
	if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !length || length > 65536)
		return 0;
	descriptor = udb_storage_alloc(budget, UDB_STORAGE_MEMORY_AUX, length);
	if (!descriptor)
		return 0;
	if (!GetKernelObjectSecurity(handle, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, descriptor, length,
								 &length) ||
		!IsValidSecurityDescriptor(descriptor) || !GetSecurityDescriptorOwner(descriptor, &owner, &defaulted) ||
		!owner || !IsValidSid(owner) || !EqualSid(owner, security->user.align.User.Sid) ||
		!GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted) || !present || !acl || !IsValidAcl(acl) ||
		acl->AceCount != 1 || !GetSecurityDescriptorControl(descriptor, &control, &revision) ||
		!(control & SE_DACL_PROTECTED) || !GetAce(acl, 0, (void **)&ace) ||
		ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE || ace->Header.AceFlags || ace->Mask != FILE_ALL_ACCESS ||
		ace->Header.AceSize < offsetof(ACCESS_ALLOWED_ACE, SidStart) + 8 ||
		((SID *)&ace->SidStart)->SubAuthorityCount > SID_MAX_SUB_AUTHORITIES ||
		GetSidLengthRequired(((SID *)&ace->SidStart)->SubAuthorityCount) >
			ace->Header.AceSize - offsetof(ACCESS_ALLOWED_ACE, SidStart) ||
		!IsValidSid((PSID)&ace->SidStart) ||
		GetLengthSid((PSID)&ace->SidStart) > ace->Header.AceSize - offsetof(ACCESS_ALLOWED_ACE, SidStart) ||
		!EqualSid((PSID)&ace->SidStart, security->user.align.User.Sid))
		goto cleanup;
	valid = 1;
cleanup:
	udb_storage_free(descriptor);
	return valid;
}

static int udb_storage_win_local_ntfs(const wchar_t *path)
{
	wchar_t volume[UDB_STORAGE_IO_PATH_MAX], filesystem[32];
	DWORD flags;

	if (!GetVolumePathNameW(path, volume, UDB_STORAGE_IO_PATH_MAX) || GetDriveTypeW(volume) != DRIVE_FIXED)
		return 0;
	if (!GetVolumeInformationW(volume, NULL, 0, NULL, NULL, &flags, filesystem,
							   (DWORD)(sizeof(filesystem) / sizeof(filesystem[0]))))
		return 0;
	return !_wcsicmp(filesystem, L"NTFS");
}
#else
static int udb_storage_posix_directory_valid(int fd)
{
	struct stat st;

	return !fstat(fd, &st) && S_ISDIR(st.st_mode) && st.st_uid == geteuid() && (st.st_mode & 0777) == 0700;
}

static int udb_storage_posix_file_valid(int fd)
{
	struct stat st;

	return !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_uid == geteuid() && (st.st_mode & 0777) == 0600 &&
		   st.st_nlink == 1;
}

static int udb_storage_posix_flush(int fd)
{
	int result;

	do
		result = fsync(fd);
	while (result < 0 && errno == EINTR);
	return result == 0;
}
#endif

static UdbStorageResult udb_storage_directory_open(UdbStorageDirectory *directory, UdbStorageBudget *budget,
												   const char *parent)
{
	int created = 0;

	if (!directory || !budget || !budget->initialized || !parent || !*parent ||
		strlen(parent) > UDB_STORAGE_IO_PATH_MAX - 6)
		return UDB_STORAGE_ERROR;
	if (directory->opened)
		return UDB_STORAGE_BUSY;
	if (udb_storage_budget_charge(budget, UDB_STORAGE_MEMORY_AUX, sizeof(*directory)) != UDB_STORAGE_OK)
		return UDB_STORAGE_LIMIT;
	memset(directory, 0, sizeof(*directory));
#ifdef _WIN32
	{
		UdbStorageWinSecurity security;
		BY_HANDLE_FILE_INFORMATION information;
		int length =
			MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, parent, -1, directory->path, UDB_STORAGE_IO_PATH_MAX);

		directory->handle = INVALID_HANDLE_VALUE;
		if (!length || !udb_storage_win_security_init(&security))
			goto fail;
		length--;
		while (length && (directory->path[length - 1] == L'\\' || directory->path[length - 1] == L'/'))
			length--;
		memcpy(directory->path + length, L"\\udb", 5 * sizeof(wchar_t));
		if (!udb_storage_win_local_ntfs(directory->path))
			goto fail;
		if (CreateDirectoryW(directory->path, &security.attributes))
			created = 1;
		else if (GetLastError() != ERROR_ALREADY_EXISTS)
			goto fail;
		directory->handle = CreateFileW(directory->path, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | READ_CONTROL,
										FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
										FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
		if (directory->handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(directory->handle, &information) ||
			!(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
			(information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
			!udb_storage_win_owner_only(directory->handle, budget, &security))
			goto fail;
		/* NTFS provisioning durability belongs to the journal/manifest barrier,
		 * not a fictitious POSIX directory-fsync emulation. */
		(void)created;
	}
#else
	{
		int parent_fd = open(parent, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);

		directory->handle = -1;
		if (parent_fd < 0)
			goto fail;
		if (!mkdirat(parent_fd, "udb", 0700))
			created = 1;
		else if (errno != EEXIST)
		{
			close(parent_fd);
			goto fail;
		}
		directory->handle = openat(parent_fd, "udb", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		if (directory->handle < 0 || !udb_storage_posix_directory_valid(directory->handle) ||
			(created && !udb_storage_posix_flush(parent_fd)))
		{
			close(parent_fd);
			goto fail;
		}
		if (close(parent_fd))
			goto fail;
	}
#endif
	directory->budget = budget;
	directory->opened = 1;
	return UDB_STORAGE_OK;
fail:
#ifdef _WIN32
	if (directory->handle != INVALID_HANDLE_VALUE)
		CloseHandle(directory->handle);
#else
	if (directory->handle >= 0)
		close(directory->handle);
#endif
	udb_storage_budget_uncharge(budget, UDB_STORAGE_MEMORY_AUX, sizeof(*directory));
	memset(directory, 0, sizeof(*directory));
	return UDB_STORAGE_ERROR;
}

static UdbStorageResult udb_storage_directory_close(UdbStorageDirectory *directory)
{
	UdbStorageResult result;

	if (!directory || !directory->opened)
		return UDB_STORAGE_ERROR;
	if (directory->open_files)
		return UDB_STORAGE_BUSY;
#ifdef _WIN32
	result = CloseHandle(directory->handle) ? UDB_STORAGE_OK : UDB_STORAGE_ERROR;
	if (result != UDB_STORAGE_OK)
		return result;
#else
	result = close(directory->handle) == 0 ? UDB_STORAGE_OK : UDB_STORAGE_ERROR;
#endif
	udb_storage_budget_uncharge(directory->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*directory));
	memset(directory, 0, sizeof(*directory));
	return result;
}

static UdbStorageResult udb_storage_file_open(UdbStorageDirectory *directory, const char *name, int create,
											  UdbStorageFile *file)
{
	UdbStorageResult result = UDB_STORAGE_ERROR;
	int created = 0;

	if (!directory || !directory->opened || !file || !udb_storage_file_name_valid(name) ||
		directory->open_files == SIZE_MAX || create < 0 || create > 3 || (create == 3 && strncmp(name, "stage_", 6)))
		return UDB_STORAGE_ERROR;
	if (file->opened)
		return UDB_STORAGE_BUSY;
	if (udb_storage_budget_charge(directory->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*file)) != UDB_STORAGE_OK)
		return UDB_STORAGE_LIMIT;
	memset(file, 0, sizeof(*file));
	udb_storage_mutex_lock(&directory->budget->mutex);
	if (!directory->budget->next_file_identity || directory->budget->open_files == SIZE_MAX)
	{
		udb_storage_mutex_unlock(&directory->budget->mutex);
		udb_storage_budget_uncharge(directory->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*file));
		return UDB_STORAGE_LIMIT;
	}
	file->identity = directory->budget->next_file_identity++;
	udb_storage_mutex_unlock(&directory->budget->mutex);
#ifdef _WIN32
	{
		UdbStorageWinSecurity security;
		BY_HANDLE_FILE_INFORMATION information;
		wchar_t path[UDB_STORAGE_IO_PATH_MAX];
		size_t length = wcslen(directory->path), name_length = strlen(name);

		file->handle = INVALID_HANDLE_VALUE;
		if (length + name_length + 2 > UDB_STORAGE_IO_PATH_MAX || !udb_storage_win_security_init(&security))
			goto fail;
		memcpy(path, directory->path, length * sizeof(wchar_t));
		path[length++] = L'\\';
		for (size_t i = 0; i <= name_length; i++)
			path[length + i] = (wchar_t)(unsigned char)name[i];
		if (create == 1 || create == 2)
		{
			file->handle =
				CreateFileW(path, GENERIC_READ | GENERIC_WRITE | READ_CONTROL, 0, &security.attributes, CREATE_NEW,
							FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_OVERLAPPED, NULL);
			if (file->handle != INVALID_HANDLE_VALUE)
				created = 1;
			else if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
				goto win_error;
		}
		if (!created && create == 2)
		{
			result = UDB_STORAGE_BUSY;
			goto fail;
		}
		if (!created)
			file->handle =
				CreateFileW(path, GENERIC_READ | GENERIC_WRITE | READ_CONTROL | (create == 3 ? DELETE : 0), 0, NULL,
							OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_OVERLAPPED, NULL);
		if (file->handle == INVALID_HANDLE_VALUE)
			goto win_error;
		if (GetFileType(file->handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(file->handle, &information) ||
			(information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
			information.nNumberOfLinks != 1 ||
			!udb_storage_win_owner_only(file->handle, directory->budget, &security) ||
			(created && !FlushFileBuffers(file->handle)))
			goto fail;
		goto win_done;
	win_error:
		if (GetLastError() == ERROR_SHARING_VIOLATION || GetLastError() == ERROR_LOCK_VIOLATION)
			result = UDB_STORAGE_BUSY;
		else if (GetLastError() == ERROR_FILE_NOT_FOUND)
			result = UDB_STORAGE_NOT_FOUND;
		goto fail;
	win_done:;
	}
#else
	file->handle = -1;
	if (create == 1 || create == 2)
	{
		file->handle = openat(directory->handle, name, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
		if (file->handle >= 0)
			created = 1;
		else if (errno != EEXIST)
			goto posix_error;
	}
	if (!created && create == 2)
	{
		result = UDB_STORAGE_BUSY;
		goto fail;
	}
	if (!created)
		file->handle = openat(directory->handle, name, O_RDWR | O_NOFOLLOW | O_CLOEXEC);
	if (file->handle < 0)
		goto posix_error;
	if (!udb_storage_posix_file_valid(file->handle))
		goto fail;
	if (flock(file->handle, LOCK_EX | LOCK_NB))
	{
		result = errno == EWOULDBLOCK || errno == EAGAIN ? UDB_STORAGE_BUSY : UDB_STORAGE_ERROR;
		goto fail;
	}
	if (created && (!udb_storage_posix_flush(file->handle) || !udb_storage_posix_flush(directory->handle)))
		goto fail;
	goto posix_done;
posix_error:
	if (errno == ENOENT)
		result = UDB_STORAGE_NOT_FOUND;
	goto fail;
posix_done:;
#endif
	file->directory = directory;
	file->opened = 1;
	directory->open_files++;
	udb_storage_mutex_lock(&directory->budget->mutex);
	directory->budget->open_files++;
	udb_storage_mutex_unlock(&directory->budget->mutex);
	if (directory->budget->cache)
		udb_storage_cache_attach_file(directory->budget->cache, file);
	return UDB_STORAGE_OK;
fail:
#ifdef _WIN32
	if (file->handle != INVALID_HANDLE_VALUE)
		CloseHandle(file->handle);
#else
	if (file->handle >= 0)
		close(file->handle);
#endif
	udb_storage_budget_uncharge(directory->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*file));
	memset(file, 0, sizeof(*file));
	return result;
}

static UdbStorageResult udb_storage_file_close(UdbStorageFile *file)
{
	UdbStorageResult result;

	if (!file || !file->opened)
		return UDB_STORAGE_ERROR;
	result = udb_storage_cache_detach_file(file);
	if (result != UDB_STORAGE_OK)
		return result;
#ifdef _WIN32
	result = CloseHandle(file->handle) ? UDB_STORAGE_OK : UDB_STORAGE_ERROR;
	if (result != UDB_STORAGE_OK)
		return result;
#else
	/* On Linux close(EINTR) has already released the descriptor; never retry. */
	result = close(file->handle) == 0 ? UDB_STORAGE_OK : UDB_STORAGE_ERROR;
#endif
	file->directory->open_files--;
	udb_storage_mutex_lock(&file->directory->budget->mutex);
	file->directory->budget->open_files--;
	udb_storage_mutex_unlock(&file->directory->budget->mutex);
	udb_storage_budget_uncharge(file->directory->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*file));
	memset(file, 0, sizeof(*file));
	return result;
}

/* Only caller-owned, quiesced private staging files may be removed. Stable
 * containers/WAL/metadata never use unlink/rename as a commit mechanism. */
static UdbStorageResult udb_storage_file_remove_private(UdbStorageDirectory *directory, const char *name)
{
	if (!directory || !directory->opened || !udb_storage_file_name_valid(name) || strncmp(name, "stage_", 6))
		return UDB_STORAGE_ERROR;
#ifdef _WIN32
	{
		wchar_t path[UDB_STORAGE_IO_PATH_MAX];
		size_t length = wcslen(directory->path), name_length = strlen(name);
		if (length + name_length + 2 > UDB_STORAGE_IO_PATH_MAX)
			return UDB_STORAGE_LIMIT;
		memcpy(path, directory->path, length * sizeof(*path));
		path[length++] = L'\\';
		for (size_t i = 0; i <= name_length; i++)
			path[length + i] = (wchar_t)(unsigned char)name[i];
		if (!DeleteFileW(path) && GetLastError() != ERROR_FILE_NOT_FOUND)
			return UDB_STORAGE_ERROR;
		return UDB_STORAGE_OK;
	}
#else
	if (unlinkat(directory->handle, name, 0) && errno != ENOENT)
		return UDB_STORAGE_ERROR;
	return udb_storage_posix_flush(directory->handle) ? UDB_STORAGE_OK : UDB_STORAGE_ERROR;
#endif
}

#ifdef _WIN32
static UdbStorageResult udb_storage_win_transfer(UdbStorageFile *file, uint64_t offset, void *bytes, size_t length,
												 int write)
{
	unsigned char *position = bytes;
	OVERLAPPED operation;
	HANDLE event = CreateEventW(NULL, TRUE, FALSE, NULL);
	UdbStorageResult result = UDB_STORAGE_ERROR;

	if (!event)
		return result;
	while (length)
	{
		DWORD chunk = length > 1024 * 1024 ? 1024 * 1024 : (DWORD)length;
		DWORD transferred = 0;
		BOOL success;

		memset(&operation, 0, sizeof(operation));
		operation.Offset = (DWORD)offset;
		operation.OffsetHigh = (DWORD)(offset >> 32);
		operation.hEvent = event;
		if (!ResetEvent(event))
			goto cleanup;
		success = write ? WriteFile(file->handle, position, chunk, NULL, &operation)
						: ReadFile(file->handle, position, chunk, NULL, &operation);
		if (!success && GetLastError() != ERROR_IO_PENDING)
			goto cleanup;
		if (!GetOverlappedResult(file->handle, &operation, &transferred, TRUE))
		{
			/* Cancellation does not itself complete an operation. Do not let
			 * a pending request retain this stack OVERLAPPED or caller buffer. */
			CancelIoEx(file->handle, &operation);
			GetOverlappedResult(file->handle, &operation, &transferred, TRUE);
			goto cleanup;
		}
		if (!transferred || transferred > chunk)
			goto cleanup;
		position += transferred;
		offset += transferred;
		length -= transferred;
	}
	result = UDB_STORAGE_OK;
cleanup:
	if (!CloseHandle(event))
		result = UDB_STORAGE_ERROR;
	return result;
}
#endif

static UdbStorageResult udb_storage_file_read(UdbStorageFile *file, uint64_t offset, void *bytes, size_t length)
{
	if (!file || !file->opened || (length && !bytes) || !udb_storage_io_range_valid(offset, length))
		return UDB_STORAGE_ERROR;
	if (!length)
		return UDB_STORAGE_OK;
#ifdef _WIN32
	return udb_storage_win_transfer(file, offset, bytes, length, 0);
#else
	{
		unsigned char *position = bytes;

		while (length)
		{
			size_t chunk = length > 1024 * 1024 ? 1024 * 1024 : length;
			ssize_t transferred = pread(file->handle, position, chunk, (off_t)offset);

			if (transferred < 0 && errno == EINTR)
				continue;
			if (transferred <= 0 || (size_t)transferred > chunk)
				return UDB_STORAGE_ERROR;
			position += transferred;
			offset += (uint64_t)transferred;
			length -= (size_t)transferred;
		}
		return UDB_STORAGE_OK;
	}
#endif
}

static UdbStorageResult udb_storage_file_write_native(UdbStorageFile *file, uint64_t offset, const void *bytes,
													  size_t length)
{
#ifdef _WIN32
	return udb_storage_win_transfer(file, offset, (void *)bytes, length, 1);
#else
	{
		const unsigned char *position = bytes;

		while (length)
		{
			size_t chunk = length > 1024 * 1024 ? 1024 * 1024 : length;
			ssize_t transferred = pwrite(file->handle, position, chunk, (off_t)offset);

			if (transferred < 0 && errno == EINTR)
				continue;
			if (transferred <= 0 || (size_t)transferred > chunk)
				return UDB_STORAGE_ERROR;
			position += transferred;
			offset += (uint64_t)transferred;
			length -= (size_t)transferred;
		}
		return UDB_STORAGE_OK;
	}
#endif
}

static UdbStorageResult udb_storage_file_write(UdbStorageFile *file, uint64_t offset, const void *bytes, size_t length)
{
	UdbStorageResult result;

	if (!file || !file->opened || (length && !bytes) || !udb_storage_io_range_valid(offset, length))
		return UDB_STORAGE_ERROR;
	if (file->append_limit && (offset > file->append_limit || length > file->append_limit - offset))
		return UDB_STORAGE_LIMIT;
	if (file->append_limit)
	{
		if (length > file->write_remaining)
			return UDB_STORAGE_LIMIT;
		file->write_remaining -= length;
	}
	if (!length)
		return UDB_STORAGE_OK;
	if (!file->cache)
		return udb_storage_file_write_native(file, offset, bytes, length);
	/* Cache -> budget is the only lock order. Hold the cache through the write
	 * so a concurrent acquire cannot re-admit stale bytes after invalidation. */
	udb_storage_mutex_lock(&file->cache->mutex);
	result = udb_storage_cache_forget_range_locked(file->cache, file->identity, offset, length);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_file_write_native(file, offset, bytes, length);
	udb_storage_mutex_unlock(&file->cache->mutex);
	return result;
}

static UdbStorageResult udb_storage_file_size(UdbStorageFile *file, uint64_t *size)
{
	if (!file || !file->opened || !size)
		return UDB_STORAGE_ERROR;
#ifdef _WIN32
	{
		LARGE_INTEGER value;

		if (!GetFileSizeEx(file->handle, &value) || value.QuadPart < 0)
			return UDB_STORAGE_ERROR;
		*size = (uint64_t)value.QuadPart;
	}
#else
	{
		struct stat st;

		if (fstat(file->handle, &st) || st.st_size < 0)
			return UDB_STORAGE_ERROR;
		*size = (uint64_t)st.st_size;
	}
#endif
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_file_resize_native(UdbStorageFile *file, uint64_t size)
{
#ifdef _WIN32
	{
		LARGE_INTEGER position;

		position.QuadPart = (LONGLONG)size;
		if (!SetFilePointerEx(file->handle, position, NULL, FILE_BEGIN) || !SetEndOfFile(file->handle))
			return UDB_STORAGE_ERROR;
	}
#else
	{
		int error;

		do
			error = ftruncate(file->handle, (off_t)size);
		while (error < 0 && errno == EINTR);
		if (error)
			return UDB_STORAGE_ERROR;
	}
#endif
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_file_resize(UdbStorageFile *file, uint64_t size)
{
	UdbStorageResult result;
	uint64_t previous;

	if (!file || !file->opened || size > INT64_MAX)
		return UDB_STORAGE_ERROR;
	if (file->append_limit && size > file->append_limit)
		return UDB_STORAGE_LIMIT;
	if (file->cache)
		udb_storage_mutex_lock(&file->cache->mutex);
	result = udb_storage_file_size(file, &previous);
	if (result == UDB_STORAGE_OK && file->cache && size < previous)
		result = udb_storage_cache_forget_range_locked(file->cache, file->identity, size, UINT64_MAX - size);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_file_resize_native(file, size);
	if (file->cache)
		udb_storage_mutex_unlock(&file->cache->mutex);
	return result;
}

static UdbStorageResult udb_storage_file_flush(UdbStorageFile *file)
{
	if (!file || !file->opened)
		return UDB_STORAGE_ERROR;
#ifdef _WIN32
	return FlushFileBuffers(file->handle) ? UDB_STORAGE_OK : UDB_STORAGE_ERROR;
#else
	return udb_storage_posix_flush(file->handle) ? UDB_STORAGE_OK : UDB_STORAGE_ERROR;
#endif
}

static void udb_storage_file_job_limit(UdbStorageFile *file, uint64_t limit)
{
	uint64_t size = 0;
	file->append_limit = limit;
	file->write_remaining = 0;
	if (limit && udb_storage_file_size(file, &size) == UDB_STORAGE_OK && size <= limit)
		file->write_remaining = limit - size;
}
