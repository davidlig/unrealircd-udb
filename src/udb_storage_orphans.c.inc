/* Startup-only private cleanup, after full stable recovery and before writer
 * admission. A filename alone never authorizes deletion of an unknown file. */

typedef struct UdbStoragePrivateName
{
	unsigned char nonce[16];
	unsigned char block;
	char role;
} UdbStoragePrivateName;

static int udb_storage_private_name(const char *name, UdbStoragePrivateName *decoded)
{
	if (!name || strlen(name) != 45 || memcmp(name, "stage_", 6) || name[38] != '_' || name[40] != '_' ||
		strcmp(name + 42, ".db") || !udb_storage_block_valid((unsigned char)name[39]) || !strchr("01m", name[41]))
		return 0;
	memset(decoded, 0, sizeof(*decoded));
	decoded->block = (unsigned char)name[39];
	decoded->role = name[41];
	for (unsigned int i = 0; i < 16; i++)
	{
		unsigned char a = (unsigned char)name[6 + 2 * i], b = (unsigned char)name[7 + 2 * i];
		int high = a >= '0' && a <= '9' ? a - '0' : a >= 'a' && a <= 'f' ? a - 'a' + 10 : -1;
		int low = b >= '0' && b <= '9' ? b - '0' : b >= 'a' && b <= 'f' ? b - 'a' + 10 : -1;
		if (high < 0 || low < 0)
			return 0;
		decoded->nonce[i] = (unsigned char)((high << 4) | low);
	}
	return 1;
}

static UdbStorageResult udb_storage_private_header(UdbStorageFile *file, const char *name)
{
	UdbStoragePrivateName decoded;
	if (!file || !file->opened || !udb_storage_private_name(name, &decoded) || decoded.role == 'm' ||
		udb_storage_zero(file->directory->store_identity, 16))
		return UDB_STORAGE_ERROR;
	unsigned char *bytes = udb_storage_alloc(file->directory->budget, UDB_STORAGE_MEMORY_AUX, UDB_STORAGE_PAGE_SIZE);
	if (!bytes)
		return UDB_STORAGE_LIMIT;
	unsigned char payload[48] = {0};
	memcpy(payload, "UDBTMP4", 7);
	memcpy(payload + 8, file->directory->store_identity, 16);
	memcpy(payload + 24, decoded.nonce, 16);
	payload[40] = decoded.block;
	payload[41] = (unsigned char)decoded.role;
	UdbStoragePage page = {0};
	page.kind = UDB_STORAGE_PAGE_METADATA;
	page.generation = page.object = 1;
	page.payload.data = payload;
	page.payload.length = sizeof(payload);
	UdbStorageResult result = udb_storage_page_encode(&page, bytes, UDB_STORAGE_PAGE_SIZE);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_file_write(file, 0, bytes, UDB_STORAGE_PAGE_SIZE);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_file_flush(file);
	udb_storage_free(bytes);
	return result;
}

static UdbStorageResult udb_storage_orphan_remove(UdbStorageDirectory *directory, const char *name,
												  unsigned char *bytes)
{
	UdbStoragePrivateName decoded;
	if (!udb_storage_private_name(name, &decoded))
		return UDB_STORAGE_OK;
	UdbStorageFile file = {0};
	/* Mode 3 opens an existing private file, with native DELETE authority on
	 * Windows. It never provisions/truncates a file, and retains exclusive access. */
	UdbStorageResult result = udb_storage_file_open(directory, name, 3, &file);
	if (result != UDB_STORAGE_OK)
		return UDB_STORAGE_OK; /* Foreign, unsafe, locked or vanished: preserve. */
	UdbStoragePage page;
	int owned = udb_storage_file_read(&file, 0, bytes, UDB_STORAGE_PAGE_SIZE) == UDB_STORAGE_OK &&
				udb_storage_page_decode(bytes, UDB_STORAGE_PAGE_SIZE, &page) == UDB_STORAGE_OK &&
				page.kind == UDB_STORAGE_PAGE_METADATA && !page.ordinal && page.payload.length >= 48 &&
				!memcmp(page.payload.data + 8, directory->store_identity, 16);
	if (owned && decoded.role == 'm')
		owned = !memcmp(page.payload.data, "UDBMAP4\0", 8) &&
				page.object == (uint64_t)udb_storage_stage_block_index(decoded.block) + 1;
	else if (owned)
		owned = page.generation == 1 && page.object == 1 && page.payload.length == 48 &&
				!memcmp(page.payload.data, "UDBTMP4\0", 8) && !memcmp(page.payload.data + 24, decoded.nonce, 16) &&
				page.payload.data[40] == decoded.block && page.payload.data[41] == (unsigned char)decoded.role &&
				udb_storage_zero(page.payload.data + 42, 6);
	result = UDB_STORAGE_OK;
	if (owned)
	{
#ifdef _WIN32
		FILE_DISPOSITION_INFO disposition;
		disposition.DeleteFile = TRUE;
		if (!SetFileInformationByHandle(file.handle, FileDispositionInfo, &disposition, sizeof(disposition)))
			result = UDB_STORAGE_ERROR;
#else
		struct stat held, named;
		if (fstat(file.handle, &held) || fstatat(directory->handle, name, &named, AT_SYMLINK_NOFOLLOW) ||
			held.st_dev != named.st_dev || held.st_ino != named.st_ino || !S_ISREG(named.st_mode) ||
			named.st_nlink != 1)
			result = UDB_STORAGE_ERROR;
		else if (unlinkat(directory->handle, name, 0) || !udb_storage_posix_flush(directory->handle))
			result = UDB_STORAGE_ERROR;
#endif
	}
	if (udb_storage_file_close(&file) != UDB_STORAGE_OK)
		result = UDB_STORAGE_ERROR;
	return result;
}

static UdbStorageResult udb_storage_orphans_cleanup(UdbStorageDirectory *directory)
{
	if (!directory || !directory->opened || udb_storage_zero(directory->store_identity, 16))
		return UDB_STORAGE_ERROR;
	/* Reserve a bounded directory-enumerator scratch allowance in addition to
	 * our explicit page buffer; no filename inventory is retained. */
	if (udb_storage_budget_charge(directory->budget, UDB_STORAGE_MEMORY_AUX, UDB_STORAGE_PAGE_SIZE) != UDB_STORAGE_OK)
		return UDB_STORAGE_LIMIT;
	unsigned char *bytes = udb_storage_alloc(directory->budget, UDB_STORAGE_MEMORY_AUX, UDB_STORAGE_PAGE_SIZE);
	if (!bytes)
	{
		udb_storage_budget_uncharge(directory->budget, UDB_STORAGE_MEMORY_AUX, UDB_STORAGE_PAGE_SIZE);
		return UDB_STORAGE_LIMIT;
	}
	UdbStorageResult result = UDB_STORAGE_OK;
#ifdef _WIN32
	wchar_t pattern[UDB_STORAGE_IO_PATH_MAX];
	size_t length = wcslen(directory->path);
	if (length + 4 > UDB_STORAGE_IO_PATH_MAX)
		result = UDB_STORAGE_LIMIT;
	else
	{
		memcpy(pattern, directory->path, length * sizeof(wchar_t));
		memcpy(pattern + length, L"\\*", 3 * sizeof(wchar_t));
		WIN32_FIND_DATAW entry;
		HANDLE search = FindFirstFileW(pattern, &entry);
		if (search == INVALID_HANDLE_VALUE)
			result = GetLastError() == ERROR_FILE_NOT_FOUND ? UDB_STORAGE_OK : UDB_STORAGE_ERROR;
		else
		{
			do
			{
				char name[46];
				size_t i = 0;
				while (i < 45 && entry.cFileName[i] && entry.cFileName[i] < 128)
				{
					name[i] = (char)entry.cFileName[i];
					i++;
				}
				if (entry.cFileName[i])
					continue;
				name[i] = 0;
				result = udb_storage_orphan_remove(directory, name, bytes);
			} while (result == UDB_STORAGE_OK && FindNextFileW(search, &entry));
			if (result == UDB_STORAGE_OK && GetLastError() != ERROR_NO_MORE_FILES)
				result = UDB_STORAGE_ERROR;
			if (!FindClose(search))
				result = UDB_STORAGE_ERROR;
		}
	}
#else
	int fd = openat(directory->handle, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	DIR *search = fd >= 0 ? fdopendir(fd) : NULL;
	if (!search)
	{
		if (fd >= 0)
			close(fd);
		result = UDB_STORAGE_ERROR;
	}
	else
	{
		for (;;)
		{
			errno = 0;
			struct dirent *entry = readdir(search);
			if (!entry)
			{
				if (errno)
					result = UDB_STORAGE_ERROR;
				break;
			}
			result = udb_storage_orphan_remove(directory, entry->d_name, bytes);
			if (result != UDB_STORAGE_OK)
				break;
		}
		if (closedir(search))
			result = UDB_STORAGE_ERROR;
	}
#endif
	udb_storage_free(bytes);
	udb_storage_budget_uncharge(directory->budget, UDB_STORAGE_MEMORY_AUX, UDB_STORAGE_PAGE_SIZE);
	return result;
}
