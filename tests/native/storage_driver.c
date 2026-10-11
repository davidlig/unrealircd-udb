#define _GNU_SOURCE
#include "../../src/udb_storage.h"
#include <assert.h>
#include <inttypes.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <psapi.h>
#include <tlhelp32.h>
#include <winioctl.h>
#else
#include <unistd.h>
#include <sys/resource.h>
#endif
#ifdef _WIN32
static volatile LONG64 test_write_bytes;
#define atomic_fetch_add(object, value) InterlockedAdd64((volatile LONG64 *)(object), (LONG64)(value))
#define atomic_store(object, value) InterlockedExchange64((volatile LONG64 *)(object), (LONG64)(value))
#define atomic_load(object) InterlockedCompareExchange64((volatile LONG64 *)(object), 0, 0)
#else
#include <stdatomic.h>
static atomic_uint_fast64_t test_write_bytes;
#endif
#include "../../src/udb_storage_cache.c.inc"
#include "../../src/udb_storage_catalog.c.inc"
#include "../../src/udb_storage_codec.c.inc"
#include "../../src/udb_storage_engine.c.inc"
#include "../../src/udb_storage_fragments.c.inc"
#include "../../src/udb_storage_hash.c.inc"
#ifdef _WIN32
/* Intercept only real Win32 calls in this test translation unit. */
static HANDLE fault_handle = INVALID_HANDLE_VALUE;
static int fault_mode;
typedef BOOL(WINAPI *UdbWriteFileApi)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef BOOL(WINAPI *UdbFlushFileBuffersApi)(HANDLE);
static UdbWriteFileApi real_write_file = WriteFile;
static UdbFlushFileBuffersApi real_flush_file_buffers = FlushFileBuffers;

static void test_process_exit(UINT code)
{
	ExitProcess(code);
	abort();
}

static BOOL WINAPI test_write_file(HANDLE handle, LPCVOID bytes, DWORD length, LPDWORD written,
								   LPOVERLAPPED overlapped)
{
	if (handle == fault_handle)
	{
		if (fault_mode == 2)
		{
			SetLastError(ERROR_DISK_FULL);
			return FALSE;
		}
		if (fault_mode == 4)
			test_process_exit(86);
		if (fault_mode == 5)
		{
			OVERLAPPED partial;
			DWORD transferred = 0;
			DWORD chunk = length / 2;
			BOOL success;

			if (!overlapped || chunk == 0)
				test_process_exit(87);
			partial = *overlapped;
			success = real_write_file(handle, bytes, chunk, NULL, &partial);
			if (!success && GetLastError() != ERROR_IO_PENDING)
				test_process_exit(87);
			if (!GetOverlappedResult(handle, &partial, &transferred, TRUE) || transferred != chunk)
				test_process_exit(87);
			test_process_exit(86);
		}
	}

	BOOL success = real_write_file(handle, bytes, length, written, overlapped);
	if (!success && GetLastError() != ERROR_IO_PENDING)
		return FALSE;
	DWORD transferred = 0;
	if (!overlapped || !GetOverlappedResult(handle, overlapped, &transferred, TRUE) || !transferred)
		return FALSE;
	atomic_fetch_add(&test_write_bytes, (uint64_t)transferred);
	return TRUE;
}

static BOOL WINAPI test_flush_file_buffers(HANDLE handle)
{
	if (handle == fault_handle && (fault_mode == 1 || fault_mode == 3))
	{
		SetLastError(ERROR_WRITE_FAULT);
		return FALSE;
	}
	BOOL success = real_flush_file_buffers(handle);
	if (handle == fault_handle && fault_mode == 6 && success)
		test_process_exit(86);
	return success;
}

#define WriteFile test_write_file
#define FlushFileBuffers test_flush_file_buffers
#else
/* POSIX fault injection also surrounds only real calls in this translation unit. */
static int fault_fd = -1, fault_mode;
static int test_fsync(int fd)
{
    if (fd == fault_fd && (fault_mode == 1 || fault_mode == 3)) { errno=EIO; return -1; }
    int result=fsync(fd);
    if (fd == fault_fd && fault_mode == 6 && !result) _exit(86);
    return result;
}
static ssize_t test_pwrite(int fd, const void *bytes, size_t n, off_t offset)
{
    if (fd == fault_fd) {
        if (fault_mode == 2) { errno=ENOSPC; return -1; }
        if (fault_mode == 4) _exit(86);
        if (fault_mode == 5) {
            if (pwrite(fd,bytes,n/2,offset)!=(ssize_t)(n/2)) _exit(87);
            _exit(86);
        }
    }
    ssize_t result=pwrite(fd,bytes,n,offset);
    if(result>0) atomic_fetch_add(&test_write_bytes,(uint64_t)result);
    return result;
}
#define fsync test_fsync
#define pwrite test_pwrite
#endif
#include "../../src/udb_storage_io.c.inc"
#ifdef _WIN32
#undef WriteFile
#undef FlushFileBuffers
#else
#undef pwrite
#undef fsync
#endif
#include "../../src/udb_storage_jobs.c.inc"
#include "../../src/udb_storage_journal.c.inc"
#include "../../src/udb_storage_nodes.c.inc"
#include "../../src/udb_storage_orphans.c.inc"
#include "../../src/udb_storage_point.c.inc"
#include "../../src/udb_storage_prefix.c.inc"
#include "../../src/udb_storage_queries.c.inc"
#include "../../src/udb_storage_raw.c.inc"
#include "../../src/udb_storage_read.c.inc"
#include "../../src/udb_storage_rebuild.c.inc"
#include "../../src/udb_storage_reclaim.c.inc"
#include "../../src/udb_storage_runs.c.inc"
#include "../../src/udb_storage_sort.c.inc"
#include "../../src/udb_storage_stage_jobs.c.inc"
#include "../../src/udb_storage_stages.c.inc"
#include "../../src/udb_storage_state.c.inc"
#include "../../src/udb_storage_validate.c.inc"

/* Final-only contract harness: uses the production engine, never a model store.
 * Validators here exercise storage structure, not daemon business profiles. */
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
#define EXPECT(e, n) do { int actual = (e); if (actual != (n)) { fprintf(stderr, "%s:%d: %s = %d, expected %d\n", __FILE__, __LINE__, #e, actual, (n)); exit(1); } } while (0)
static int cancel_at_prepare;
static UdbStorageResult row_valid(const UdbStorageRecord *record, void *unused)
{
    (void)unused;
    return udb_storage_row_valid(&record->row) ? UDB_STORAGE_OK : UDB_STORAGE_REJECTED;
}
static UdbStorageResult stage_valid(const UdbStorageSource *source, unsigned char block, void *unused)
{
    (void)source; (void)block; (void)unused;
    return UDB_STORAGE_OK;
}
static UdbStorageResult mutation_valid(UdbStorageEngine *e, const UdbStorageBlockRoot *r, const UdbStorageRow *row, void *unused)
{
    (void)e; (void)r; (void)row; (void)unused;
    return UDB_STORAGE_OK;
}
static UdbStorageResult prepare(UdbStorageEngine *e, const UdbStorageJob *j, const UdbStorageState *base, UdbStorageState *out)
{
    UdbStorageResult result = udb_storage_jobs_prepare(e, j, base, out);
    if (cancel_at_prepare && result == UDB_STORAGE_OK) {
        uint64_t revision;
        EXPECT(udb_storage_engine_authority(e, &revision), UDB_STORAGE_OK);
        cancel_at_prepare = 0;
    }
    return result;
}
static void open_engine(UdbStorageEngine *e, const char *path, size_t cache)
{
    memset(e, 0, sizeof(*e));
    EXPECT(udb_storage_engine_open(e, path, cache, prepare, udb_storage_jobs_finish,
        udb_storage_jobs_cleanup, row_valid, stage_valid, mutation_valid, NULL, NULL, NULL), UDB_STORAGE_OK);
}
static uint64_t submit(UdbStorageEngine *e, unsigned int kind, uint64_t owner, const void *bytes, size_t length)
{
    uint64_t id = 0;
    EXPECT(udb_storage_engine_submit(e, kind, owner, e->module_revision, e->authority_revision, bytes, length, &id), UDB_STORAGE_OK);
    return id;
}
static UdbStorageRow key(unsigned char block, const char *a, const char *b, const char *c)
{
    UdbStorageRow r = {0};
    r.block = block;
    const char *parts[] = {a,b,c};
    for (unsigned int i=0; i<3 && parts[i]; i++) {
        r.components[i].data = (const unsigned char *)parts[i];
        r.components[i].length = (uint32_t)strlen(parts[i]); r.depth++;
    }
    return r;
}
static size_t header(unsigned char *p, unsigned char block, int local)
{
    memset(p, 0, UDB_STORAGE_MUTATION_HEADER);
    memcpy(p, "001", 4); udb_storage_put_u64(p+4, 42); p[20]=block; p[21]=(unsigned char)local;
    return UDB_STORAGE_MUTATION_HEADER;
}
static void bind_source(UdbStorageEngine *e)
{
    unsigned char p[UDB_STORAGE_MUTATION_HEADER]; header(p, 0, 0);
    submit(e, UDB_STORAGE_JOB_BIND, 1, p, sizeof(p));
}
static void mutate(UdbStorageEngine *e, unsigned int kind, UdbStorageRow row, uint64_t expiry)
{
    unsigned char p[UDB_STORAGE_MUTATION_HEADER+8+UDB_STORAGE_ROW_MAX];
    size_t n, offset=header(p,row.block,1);
    if (kind == UDB_STORAGE_JOB_EXPIRE) { udb_storage_put_u64(p+offset, expiry); offset+=8; }
    if (kind != UDB_STORAGE_JOB_DROP) {
        EXPECT(udb_storage_row_encode(&row,p+offset,sizeof(p)-offset,&n), UDB_STORAGE_OK); offset+=n;
    }
    submit(e, kind, 2, p, offset);
}
static UdbStorageJob *next_job(UdbStorageEngine *e, UdbStorageResult expected)
{
    UdbStorageJob *j=NULL;
    EXPECT(udb_storage_engine_poll(e,&j),UDB_STORAGE_OK); CHECK(j);
    EXPECT(j->result,expected);
    CHECK(expected != UDB_STORAGE_OK || j->committed_generation != 0);
    CHECK(expected == UDB_STORAGE_OK || j->committed_generation == 0);
    return j;
}
static void free_job(UdbStorageEngine *e, UdbStorageJob *j)
{
    EXPECT(udb_storage_engine_job_free(e,&j),UDB_STORAGE_OK); CHECK(!j);
}
static void drain(UdbStorageEngine *e)
{
    EXPECT(udb_storage_engine_stop(e,1),UDB_STORAGE_OK);
}
static void finish(UdbStorageEngine *e, unsigned int count)
{
    drain(e);
    for (unsigned int i=0;i<count;i++) free_job(e,next_job(e,UDB_STORAGE_OK));
    UdbStorageJob *j=NULL; EXPECT(udb_storage_engine_poll(e,&j),UDB_STORAGE_NOT_FOUND);
    CHECK(e->queue_bytes==0 && e->delivery_bytes==0);
    CHECK(e->budget.peak[1]<=UDB_STORAGE_AUX_LIMIT && e->budget.peak[0]<=e->budget.limit[0]);
    EXPECT(udb_storage_engine_close(e),UDB_STORAGE_OK);
}
static void field(UdbStorageRead *read, UdbStorageRow row, UdbStorageResult result, uint64_t number)
{
    UdbStorageField out;
    EXPECT(udb_storage_read_field(read,&row,&out),result);
    if (result==UDB_STORAGE_OK) CHECK(out.present && out.kind==UDB_STORAGE_VALUE_NUMBER && out.number==number);
    else CHECK(!out.present);
}
static void codec(void)
{
    unsigned char bytes[UDB_STORAGE_ROW_MAX]; size_t n;
    UdbStorageRow r=key('N',"ALIce","Created",NULL), decoded;
    r.kind=UDB_STORAGE_VALUE_NUMBER; r.number=UINT64_MAX;
    EXPECT(udb_storage_row_encode(&r,bytes,sizeof(bytes),&n),UDB_STORAGE_OK);
    EXPECT(udb_storage_row_decode(bytes,n,&decoded),UDB_STORAGE_OK);
    CHECK(decoded.number==UINT64_MAX && !memcmp(decoded.components[0].data,"alice",5));
    CHECK(udb_storage_key_compare(&r,&decoded)==0);
    EXPECT(udb_storage_row_decode(bytes,n-1,&decoded),UDB_STORAGE_CORRUPT);
    r=key('K',"F","CaseSensitive", "reason");
    EXPECT(udb_storage_row_encode(&r,bytes,sizeof(bytes),&n),UDB_STORAGE_OK);
    EXPECT(udb_storage_row_decode(bytes,n,&decoded),UDB_STORAGE_OK);
    CHECK(decoded.components[0].data[0]=='f' && !memcmp(decoded.components[1].data,"CaseSensitive",13));
    UdbStorageRow other=key('K',"f","casesensitive","reason"); CHECK(udb_storage_key_compare(&decoded,&other)!=0);
    r.depth=9; EXPECT(udb_storage_row_encode(&r,bytes,sizeof(bytes),&n),UDB_STORAGE_ERROR);
    unsigned char *page=calloc(1,UDB_STORAGE_PAGE_SIZE); CHECK(page);
    UdbStoragePage p={0}, q; p.kind=UDB_STORAGE_PAGE_METADATA; p.generation=1; p.object=8;
    p.payload.data=(const unsigned char *)"data"; p.payload.length=4;
    EXPECT(udb_storage_page_encode(&p,page,UDB_STORAGE_PAGE_SIZE),UDB_STORAGE_OK);
    EXPECT(udb_storage_page_decode(page,UDB_STORAGE_PAGE_SIZE,&q),UDB_STORAGE_OK);
    page[UDB_STORAGE_PAGE_HEADER]^=1;
    EXPECT(udb_storage_page_decode(page,UDB_STORAGE_PAGE_SIZE,&q),UDB_STORAGE_CORRUPT); free(page);
}
static void mutations(const char *path)
{
    UdbStorageEngine e; open_engine(&e,path,UDB_STORAGE_CACHE_DEFAULT); bind_source(&e);
    UdbStorageRow r=key('N',"alice","created",NULL); r.kind=UDB_STORAGE_VALUE_NUMBER;
    for (unsigned int i=0;i<12;i++) { r.number=i; mutate(&e,UDB_STORAGE_JOB_POINT,r,0); }
    mutate(&e,UDB_STORAGE_JOB_PREFIX,key('N',"alice",NULL,NULL),0);
    r.number=0; mutate(&e,UDB_STORAGE_JOB_POINT,r,0);
    drain(&e); free_job(&e,next_job(&e,UDB_STORAGE_OK));
    for (unsigned int i=0;i<12;i++) {
        UdbStorageJob *j=next_job(&e,UDB_STORAGE_OK); UdbStorageRead *read=NULL;
        EXPECT(udb_storage_read_job(j,1,&read),UDB_STORAGE_OK); field(read,r,UDB_STORAGE_OK,i);
        EXPECT(udb_storage_read_close(&read),UDB_STORAGE_OK); free_job(&e,j);
    }
    UdbStorageJob *j=next_job(&e,UDB_STORAGE_OK); UdbStorageRead *read=NULL;
    EXPECT(udb_storage_read_job(j,1,&read),UDB_STORAGE_OK); field(read,r,UDB_STORAGE_NOT_FOUND,0);
    EXPECT(udb_storage_read_close(&read),UDB_STORAGE_OK); free_job(&e,j);
    j=next_job(&e,UDB_STORAGE_OK); EXPECT(udb_storage_read_job(j,1,&read),UDB_STORAGE_OK);
    field(read,r,UDB_STORAGE_OK,0); EXPECT(udb_storage_read_close(&read),UDB_STORAGE_OK); free_job(&e,j);
    CHECK(e.current->state.source_sequence==14); EXPECT(udb_storage_engine_close(&e),UDB_STORAGE_OK);
    open_engine(&e,path,256*1024*1024); EXPECT(udb_storage_read_open(&e,&read),UDB_STORAGE_OK);
    field(read,key('N',"ALICE","CREATED",NULL),UDB_STORAGE_OK,0);
    unsigned char digest[32]; memcpy(digest,read->generation->state.blocks[0].summary.digest,32);
    EXPECT(udb_storage_read_close(&read),UDB_STORAGE_OK);
    mutate(&e,UDB_STORAGE_JOB_DROP,key('N',"dummy",NULL,NULL),0); finish(&e,1);
    open_engine(&e,path,512*1024*1024); EXPECT(udb_storage_read_open(&e,&read),UDB_STORAGE_OK);
    field(read,r,UDB_STORAGE_NOT_FOUND,0); CHECK(memcmp(digest,read->generation->state.blocks[0].summary.digest,32));
    EXPECT(udb_storage_read_close(&read),UDB_STORAGE_OK); EXPECT(udb_storage_engine_close(&e),UDB_STORAGE_OK);
}
static void credits(const char *path)
{
    UdbStorageEngine e; open_engine(&e,path,UDB_STORAGE_CACHE_DEFAULT);
    unsigned char p[24]; header(p,0,0); size_t charge=udb_storage_engine_job_charge(sizeof(p));
    EXPECT(udb_storage_engine_delivery_reserve(&e,1,1,UDB_STORAGE_QUEUE_LIMIT),UDB_STORAGE_OK);
    uint64_t id;
    EXPECT(udb_storage_engine_submit(&e,UDB_STORAGE_JOB_BIND,1,1,1,p,sizeof(p),&id),UDB_STORAGE_LIMIT);
    EXPECT(udb_storage_engine_submit_credit(&e,UDB_STORAGE_JOB_BIND,1,1,1,p,sizeof(p),charge,&id),UDB_STORAGE_OK);
    CHECK(e.queue_bytes==UDB_STORAGE_QUEUE_LIMIT);
    EXPECT(udb_storage_engine_delivery_release(&e,UDB_STORAGE_QUEUE_LIMIT-charge),UDB_STORAGE_OK);
    finish(&e,1);
}
static void cancellation(const char *path)
{
    UdbStorageEngine e; open_engine(&e,path,UDB_STORAGE_CACHE_DEFAULT); cancel_at_prepare=1; bind_source(&e); drain(&e);
    free_job(&e,next_job(&e,UDB_STORAGE_BUSY)); CHECK(!e.current->state.source_sid[0]);
    EXPECT(udb_storage_engine_close(&e),UDB_STORAGE_OK);
    open_engine(&e,path,UDB_STORAGE_CACHE_DEFAULT); CHECK(!e.current->state.source_sid[0]);
    EXPECT(udb_storage_engine_close(&e),UDB_STORAGE_OK);
}
static void expiry(const char *path)
{
    UdbStorageEngine e; open_engine(&e,path,UDB_STORAGE_CACHE_DEFAULT); bind_source(&e);
    UdbStorageRow r=key('K',"G","*@host", "expires"); r.kind=UDB_STORAGE_VALUE_NUMBER; r.number=200;
    mutate(&e,UDB_STORAGE_JOB_POINT,r,0);
    mutate(&e,UDB_STORAGE_JOB_EXPIRE,key('K',"G","*@host",NULL),100);
    mutate(&e,UDB_STORAGE_JOB_EXPIRE,key('K',"G","*@host",NULL),200);
    drain(&e); free_job(&e,next_job(&e,UDB_STORAGE_OK)); free_job(&e,next_job(&e,UDB_STORAGE_OK));
    free_job(&e,next_job(&e,UDB_STORAGE_STAGED)); UdbStorageJob *j=next_job(&e,UDB_STORAGE_OK);
    CHECK(j->committed_source_sequence==2); UdbStorageRead *read=NULL;
    EXPECT(udb_storage_read_job(j,1,&read),UDB_STORAGE_OK); field(read,r,UDB_STORAGE_NOT_FOUND,0);
    EXPECT(udb_storage_read_close(&read),UDB_STORAGE_OK); free_job(&e,j); EXPECT(udb_storage_engine_close(&e),UDB_STORAGE_OK);
}

static UdbStorageResult partition_one(unsigned int partition, UdbStorageLogicalSummary *summary, void *ctx)
{
    const UdbStorageLogicalSummary *items=ctx; *summary=items[partition]; return UDB_STORAGE_OK;
}
static void one_digest(UdbStorageEngine *e, UdbStorageRow r, unsigned char digest[32])
{
    UdbStorageLogicalSummary items[4096], total;
    unsigned char hash[32], bytes[UDB_STORAGE_ROW_MAX]; unsigned int partition; size_t n;
    EXPECT(udb_storage_key_hash(&r,hash,&partition),UDB_STORAGE_OK);
    for (unsigned int i=0;i<4096;i++) {
        UdbStorageLogicalPosition position={0}; position.block=r.block; position.partition=(uint16_t)i;
        EXPECT(udb_storage_logical_empty(&position,items+i),UDB_STORAGE_OK);
    }
    UdbStorageLogicalPosition position={0}; position.block=r.block; position.partition=(uint16_t)partition;
    UdbStorageLeafHasher *hasher=NULL;
    EXPECT(udb_storage_leaf_hasher_create(&e->budget,&position,&hasher),UDB_STORAGE_OK);
    EXPECT(udb_storage_row_encode(&r,bytes,sizeof(bytes),&n),UDB_STORAGE_OK);
    EXPECT(udb_storage_leaf_hasher_add(hasher,bytes,n),UDB_STORAGE_OK);
    EXPECT(udb_storage_leaf_hasher_finish(hasher,items+partition),UDB_STORAGE_OK);
    udb_storage_leaf_hasher_free(&hasher);
    EXPECT(udb_storage_logical_block(r.block,partition_one,items,&total),UDB_STORAGE_OK);
    memcpy(digest,total.digest,32);
}
static void staging(const char *path, int bad_digest, int abandoned)
{
    UdbStorageEngine e; open_engine(&e,path,UDB_STORAGE_CACHE_DEFAULT);
    UdbStorageRow r=key('N',"alice","created",NULL); r.kind=UDB_STORAGE_VALUE_NUMBER; r.number=42;
    UdbStorageStageSpec spec={0}; spec.head.owner=7; spec.head.round=3;
    memcpy(spec.head.source_sid,"001",4); spec.head.source_epoch=42; spec.head.source_sequence=900;
    spec.rows_limit=100; spec.physical_limit=128*1024*1024;
    spec.block='N'; spec.count=1; one_digest(&e,r,spec.digest);
    if (bad_digest) spec.digest[0]^=1;
    submit(&e,UDB_STORAGE_STAGE_BEGIN,7,&spec,sizeof(spec));
    unsigned char bytes[UDB_STORAGE_ROW_MAX]; size_t n;
    EXPECT(udb_storage_row_encode(&r,bytes,sizeof(bytes),&n),UDB_STORAGE_OK);
    submit(&e,UDB_STORAGE_STAGE_PUT,7,bytes,n);
    submit(&e,UDB_STORAGE_STAGE_END,7,"N",1);
    if (!bad_digest && !abandoned) {
        for (unsigned int i=1;i<6;i++) {
            spec.block=udb_storage_blocks[i]; spec.count=0;
            memcpy(spec.digest,e.current->state.blocks[i].summary.digest,32);
            submit(&e,UDB_STORAGE_STAGE_REUSE,7,&spec,sizeof(spec));
        }
        submit(&e,UDB_STORAGE_STAGE_PROMOTE,7,NULL,0);
    }
    drain(&e); free_job(&e,next_job(&e,UDB_STORAGE_STAGED)); free_job(&e,next_job(&e,UDB_STORAGE_STAGED));
    UdbStorageJob *j=next_job(&e,bad_digest ? UDB_STORAGE_REJECTED : UDB_STORAGE_OK);
    if (!bad_digest) {
        UdbStorageRead *read=NULL;
        EXPECT(udb_storage_read_job(j,1,&read),UDB_STORAGE_OK);
        field(read,r,UDB_STORAGE_NOT_FOUND,0); CHECK(read->generation->state.stages.count>0);
        EXPECT(udb_storage_read_close(&read),UDB_STORAGE_OK);
    }
    free_job(&e,j);
    if (!bad_digest && !abandoned) {
        for (unsigned int i=1;i<6;i++) free_job(&e,next_job(&e,UDB_STORAGE_OK));
        j=next_job(&e,UDB_STORAGE_OK); UdbStorageRead *read=NULL;
        EXPECT(udb_storage_read_job(j,0,&read),UDB_STORAGE_OK); field(read,r,UDB_STORAGE_NOT_FOUND,0);
        EXPECT(udb_storage_read_close(&read),UDB_STORAGE_OK);
        EXPECT(udb_storage_read_job(j,1,&read),UDB_STORAGE_OK); field(read,r,UDB_STORAGE_OK,42);
        CHECK(read->generation->state.source_sequence==900 && read->generation->state.stages.count==0);
        EXPECT(udb_storage_read_close(&read),UDB_STORAGE_OK); free_job(&e,j);
    }
    EXPECT(udb_storage_engine_close(&e),UDB_STORAGE_OK);
    open_engine(&e,path,256*1024*1024); UdbStorageRead *read=NULL;
    EXPECT(udb_storage_read_open(&e,&read),UDB_STORAGE_OK);
    field(read,r,bad_digest || abandoned ? UDB_STORAGE_NOT_FOUND : UDB_STORAGE_OK,42);
    CHECK(read->generation->state.stages.count==0);
    EXPECT(udb_storage_read_close(&read),UDB_STORAGE_OK); EXPECT(udb_storage_engine_close(&e),UDB_STORAGE_OK);
}
static void seed(const char *path, int checkpoint, int crash)
{
    UdbStorageEngine e; open_engine(&e,path,UDB_STORAGE_CACHE_DEFAULT); bind_source(&e);
    UdbStorageRow r=key('N',"alice","created",NULL); r.kind=UDB_STORAGE_VALUE_NUMBER; r.number=42;
    mutate(&e,UDB_STORAGE_JOB_POINT,r,0); drain(&e);
    free_job(&e,next_job(&e,UDB_STORAGE_OK)); free_job(&e,next_job(&e,UDB_STORAGE_OK));
    if (checkpoint) EXPECT(udb_storage_reclaimer_refresh(&e),UDB_STORAGE_OK);
    if (crash) {
        fflush(NULL);
#ifdef _WIN32
        test_process_exit(0);
#else
        _exit(0);
#endif
    }
    EXPECT(udb_storage_engine_close(&e),UDB_STORAGE_OK);
}
static void verify(const char *path, int corrupt)
{
    UdbStorageEngine e={0};
    UdbStorageResult result=udb_storage_engine_open(&e,path,UDB_STORAGE_CACHE_DEFAULT,prepare,
        udb_storage_jobs_finish,udb_storage_jobs_cleanup,row_valid,stage_valid,mutation_valid,NULL,NULL,NULL);
    if (corrupt) { EXPECT(result,UDB_STORAGE_CORRUPT); return; }
    EXPECT(result,UDB_STORAGE_OK); UdbStorageRead *read=NULL;
    EXPECT(udb_storage_read_open(&e,&read),UDB_STORAGE_OK);
    field(read,key('N',"alice","created",NULL),UDB_STORAGE_OK,42);
    CHECK(read->generation->state.source_sequence==1);
    EXPECT(udb_storage_read_close(&read),UDB_STORAGE_OK); EXPECT(udb_storage_engine_close(&e),UDB_STORAGE_OK);
}
static void io_fault(const char *path, int mode)
{
    UdbStorageEngine e; open_engine(&e,path,UDB_STORAGE_CACHE_DEFAULT);
    fault_mode=mode;
#ifdef _WIN32
    fault_handle=e.files[mode==1 ? 0 : 6].handle;
#else
    fault_fd=e.files[mode==1 ? 0 : 6].handle;
#endif
    UdbStorageRow r=key('N',"alice","created",NULL); r.kind=UDB_STORAGE_VALUE_NUMBER; r.number=43;
    mutate(&e,UDB_STORAGE_JOB_POINT,r,0); drain(&e);
    free_job(&e,next_job(&e,UDB_STORAGE_ERROR));
    CHECK(e.failed && e.current->state.source_sequence==1);
    /* A failed flush must never advertise a receipt or allow further reads. */
    UdbStorageRead *read=NULL; EXPECT(udb_storage_read_open(&e,&read),UDB_STORAGE_ERROR);
#ifdef _WIN32
    test_process_exit(86);
#else
    _exit(86);
#endif
}
#ifdef _WIN32
static int win_path_join(const char *base, const char *leaf, wchar_t *out, size_t capacity)
{
	wchar_t wide_base[UDB_STORAGE_IO_PATH_MAX];
	int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, base, -1, wide_base,
									UDB_STORAGE_IO_PATH_MAX);
	size_t length;
	if (!count)
		return 0;
	length = wcslen(wide_base);
	if (length + wcslen(leaf) + 2 > capacity)
		return 0;
	wcscpy(out, wide_base);
	if (length && out[length - 1] != L'\\' && out[length - 1] != L'/')
		wcscat(out, L"\\");
	wcscat(out, leaf);
	return 1;
}

static UdbStorageResult win_try_open(const char *path, UdbStorageEngine *engine)
{
	memset(engine, 0, sizeof(*engine));
	return udb_storage_engine_open(engine, path, UDB_STORAGE_CACHE_DEFAULT, prepare,
								   udb_storage_jobs_finish, udb_storage_jobs_cleanup, row_valid,
								   stage_valid, mutation_valid, NULL, NULL, NULL);
}

static void win_offset(const char *path)
{
	UdbStorageEngine engine;
	UdbStorageFile file = {0};
	DWORD returned = 0;
	unsigned char expected[128], actual[128];
	uint64_t offset = (UINT64_C(1) << 32) + UINT64_C(123457), size = 0;
	EXPECT(udb_storage_engine_open(&engine, path, UDB_STORAGE_CACHE_DEFAULT, prepare,
		udb_storage_jobs_finish, udb_storage_jobs_cleanup, row_valid, stage_valid, mutation_valid,
		NULL, NULL, NULL), UDB_STORAGE_OK);
	EXPECT(udb_storage_file_open(&engine.directory, "udb_offset.db", 1, &file), UDB_STORAGE_OK);
	CHECK(DeviceIoControl(file.handle, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &returned, NULL));
	for (size_t i = 0; i < sizeof(expected); i++) expected[i] = (unsigned char)(i * 29u + 7u);
	EXPECT(udb_storage_file_write(&file, offset, expected, sizeof(expected)), UDB_STORAGE_OK);
	UdbStorageResult read_result = udb_storage_file_read(&file, offset, actual, sizeof(actual));
	if (read_result != UDB_STORAGE_OK)
	{
		LARGE_INTEGER native_size = {0};
		GetFileSizeEx(file.handle, &native_size);
		fprintf(stderr, "positioned read failed: winerr=%lu size=%llu\n", GetLastError(),
			(unsigned long long)native_size.QuadPart);
	}
	EXPECT(read_result, UDB_STORAGE_OK);
	CHECK(!memcmp(expected, actual, sizeof(expected)));
	EXPECT(udb_storage_file_size(&file, &size), UDB_STORAGE_OK);
	CHECK(size == offset + sizeof(expected));
	EXPECT(udb_storage_file_close(&file), UDB_STORAGE_OK);
	EXPECT(udb_storage_engine_close(&engine), UDB_STORAGE_OK);
}

static void win_reject_store(const char *path)
{
	UdbStorageEngine engine;
	UdbStorageResult result = win_try_open(path, &engine);
	CHECK(result == UDB_STORAGE_ERROR || result == UDB_STORAGE_BUSY);
}

static void win_insecure_directory(const char *path)
{
	wchar_t directory[UDB_STORAGE_IO_PATH_MAX];
	CHECK(win_path_join(path, L"udb", directory, UDB_STORAGE_IO_PATH_MAX));
	CHECK(CreateDirectoryW(directory, NULL));
	win_reject_store(path);
}

static void win_insecure_file(const char *path)
{
	UdbStorageEngine engine;
	wchar_t file_path[UDB_STORAGE_IO_PATH_MAX];
	EXPECT(udb_storage_engine_open(&engine, path, UDB_STORAGE_CACHE_DEFAULT, prepare,
		udb_storage_jobs_finish, udb_storage_jobs_cleanup, row_valid, stage_valid, mutation_valid,
		NULL, NULL, NULL), UDB_STORAGE_OK);
	EXPECT(udb_storage_engine_close(&engine), UDB_STORAGE_OK);
	CHECK(win_path_join(path, L"udb\\udb_N.db", file_path, UDB_STORAGE_IO_PATH_MAX));
	CHECK(DeleteFileW(file_path));
	HANDLE handle = CreateFileW(file_path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW,
		FILE_ATTRIBUTE_NORMAL, NULL);
	CHECK(handle != INVALID_HANDLE_VALUE);
	CHECK(CloseHandle(handle));
	win_reject_store(path);
}

static void win_hardlink(const char *path)
{
	UdbStorageEngine engine;
	wchar_t primary[UDB_STORAGE_IO_PATH_MAX], alias[UDB_STORAGE_IO_PATH_MAX];
	EXPECT(udb_storage_engine_open(&engine, path, UDB_STORAGE_CACHE_DEFAULT, prepare,
		udb_storage_jobs_finish, udb_storage_jobs_cleanup, row_valid, stage_valid, mutation_valid,
		NULL, NULL, NULL), UDB_STORAGE_OK);
	EXPECT(udb_storage_engine_close(&engine), UDB_STORAGE_OK);
	CHECK(win_path_join(path, L"udb\\udb_N.db", primary, UDB_STORAGE_IO_PATH_MAX));
	CHECK(win_path_join(path, L"udb_link_target.db", alias, UDB_STORAGE_IO_PATH_MAX));
	CHECK(CreateHardLinkW(alias, primary, NULL));
	UdbStorageResult result = win_try_open(path, &engine);
	CHECK(result == UDB_STORAGE_ERROR || result == UDB_STORAGE_BUSY);
	CHECK(DeleteFileW(alias));
}

static void win_hold(const char *path, unsigned long milliseconds)
{
	UdbStorageEngine engine;
	EXPECT(udb_storage_engine_open(&engine, path, UDB_STORAGE_CACHE_DEFAULT, prepare,
		udb_storage_jobs_finish, udb_storage_jobs_cleanup, row_valid, stage_valid, mutation_valid,
		NULL, NULL, NULL), UDB_STORAGE_OK);
	puts("READY"); fflush(stdout);
	Sleep(milliseconds);
	EXPECT(udb_storage_engine_close(&engine), UDB_STORAGE_OK);
	puts("PASS");
}

static DWORD win_thread_count(void)
{
	DWORD process_id = GetCurrentProcessId(), count = 0;
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	THREADENTRY32 entry = {sizeof(entry)};
	CHECK(snapshot != INVALID_HANDLE_VALUE);
	if (Thread32First(snapshot, &entry))
	{
		do { if (entry.th32OwnerProcessID == process_id) count++; } while (Thread32Next(snapshot, &entry));
	}
	CHECK(CloseHandle(snapshot));
	return count;
}

static void win_close_cycles(const char *path)
{
	UdbStorageEngine engine;
	DWORD before_handles = 0, after_handles = 0;
	for (unsigned int i = 0; i < 3; i++)
	{
		EXPECT(udb_storage_engine_open(&engine, path, UDB_STORAGE_CACHE_DEFAULT, prepare,
			udb_storage_jobs_finish, udb_storage_jobs_cleanup, row_valid, stage_valid, mutation_valid,
			NULL, NULL, NULL), UDB_STORAGE_OK);
		EXPECT(udb_storage_engine_close(&engine), UDB_STORAGE_OK);
	}
	Sleep(50);
	DWORD before_threads = win_thread_count();
	CHECK(GetProcessHandleCount(GetCurrentProcess(), &before_handles));
	for (unsigned int i = 0; i < 12; i++)
	{
		EXPECT(udb_storage_engine_open(&engine, path, UDB_STORAGE_CACHE_DEFAULT, prepare,
			udb_storage_jobs_finish, udb_storage_jobs_cleanup, row_valid, stage_valid, mutation_valid,
			NULL, NULL, NULL), UDB_STORAGE_OK);
		EXPECT(udb_storage_engine_close(&engine), UDB_STORAGE_OK);
	}
	Sleep(50);
	CHECK(GetProcessHandleCount(GetCurrentProcess(), &after_handles));
	CHECK(after_handles == before_handles);
	CHECK(win_thread_count() == before_threads);
}
#endif

static void verify_fault(const char *path, int expected)
{
    UdbStorageEngine e; open_engine(&e,path,UDB_STORAGE_CACHE_DEFAULT);
    UdbStorageRead *read=NULL; EXPECT(udb_storage_read_open(&e,&read),UDB_STORAGE_OK);
    UdbStorageField out; UdbStorageRow r=key('N',"alice","created",NULL);
    EXPECT(udb_storage_read_field(read,&r,&out),UDB_STORAGE_OK);
    CHECK(out.kind==UDB_STORAGE_VALUE_NUMBER);
    CHECK(expected ? out.number==(uint64_t)expected : (out.number==42 || out.number==43));
    CHECK(read->generation->state.source_sequence==(out.number==42 ? 1 : 2));
    EXPECT(udb_storage_read_close(&read),UDB_STORAGE_OK); EXPECT(udb_storage_engine_close(&e),UDB_STORAGE_OK);
}
static void orphans(const char *path)
{
    UdbStorageEngine e; open_engine(&e,path,UDB_STORAGE_CACHE_DEFAULT);
    const char *owned="stage_00000000000000000000000000000000_N_0.db";
    const char *foreign="stage_11111111111111111111111111111111_N_0.db";
    UdbStorageFile f={0}; EXPECT(udb_storage_file_open(&e.directory,owned,2,&f),UDB_STORAGE_OK);
    EXPECT(udb_storage_private_header(&f,owned),UDB_STORAGE_OK); EXPECT(udb_storage_file_close(&f),UDB_STORAGE_OK);
    EXPECT(udb_storage_file_open(&e.directory,foreign,2,&f),UDB_STORAGE_OK);
    EXPECT(udb_storage_file_write(&f,0,"foreign",7),UDB_STORAGE_OK); EXPECT(udb_storage_file_close(&f),UDB_STORAGE_OK);
    EXPECT(udb_storage_engine_close(&e),UDB_STORAGE_OK); open_engine(&e,path,UDB_STORAGE_CACHE_DEFAULT);
    EXPECT(udb_storage_file_open(&e.directory,owned,0,&f),UDB_STORAGE_NOT_FOUND);
    EXPECT(udb_storage_file_open(&e.directory,foreign,0,&f),UDB_STORAGE_OK); EXPECT(udb_storage_file_close(&f),UDB_STORAGE_OK);
    EXPECT(udb_storage_engine_close(&e),UDB_STORAGE_OK);
}

static void split_collapse_pins(const char *path)
{
    UdbStorageEngine e; open_engine(&e,path,UDB_STORAGE_CACHE_DEFAULT); bind_source(&e); finish(&e,1);
    open_engine(&e,path,UDB_STORAGE_CACHE_DEFAULT); drain(&e);
    UdbStorageSort *sort=NULL;
    EXPECT(udb_storage_sort_create(&e.directory,'N',5000,UINT64_C(256)*1024*1024,row_valid,NULL,&sort),UDB_STORAGE_OK);
    unsigned char value[4096]; memset(value,'x',sizeof(value));
    unsigned char bytes[UDB_STORAGE_RECORD_MAX]; size_t n; char prop[32];
    for(unsigned int i=0;i<5000;i++) {
        snprintf(prop,sizeof(prop),"p%05u",i);
        UdbStorageRecord record={0}; record.operation=UDB_STORAGE_PUT; record.stamp.ordinal=i;
        record.row=key('N',"entity",prop,NULL); record.row.kind=UDB_STORAGE_VALUE_STRING;
        record.row.value.data=value; record.row.value.length=sizeof(value);
        EXPECT(udb_storage_record_encode(&record,bytes,sizeof(bytes),&n),UDB_STORAGE_OK);
        EXPECT(udb_storage_sort_add(sort,bytes,n),UDB_STORAGE_OK);
    }
    EXPECT(udb_storage_sort_seal(sort),UDB_STORAGE_OK); UdbStorageResult result;
    do { result=udb_storage_sort_step(sort); } while(result==UDB_STORAGE_BUSY);
    EXPECT(result,UDB_STORAGE_OK); UdbStorageSource source;
    EXPECT(udb_storage_sort_source(sort,1,0,&source),UDB_STORAGE_OK);
    UdbStorageRow row=key('N',"entity","p00000",NULL); unsigned int partition; unsigned char hash[32];
    EXPECT(udb_storage_key_hash(&row,hash,&partition),UDB_STORAGE_OK);
    UdbStorageLogicalPosition position={0}; position.block='N'; position.partition=(uint16_t)partition;
    UdbStorageState state=e.current->state; state.generation++; state.sequence++; state.source_sequence=1;
    UdbStorageRebuild *rebuild=NULL;
    EXPECT(udb_storage_rebuild_create(e.cache,e.files,state.blocks,&position,state.generation,1,state.next_object,&source,&rebuild),UDB_STORAGE_OK);
    do { result=udb_storage_rebuild_step(rebuild,state.blocks,&state.next_object); } while(result==UDB_STORAGE_BUSY);
    EXPECT(result,UDB_STORAGE_OK); EXPECT(udb_storage_rebuild_close(&rebuild),UDB_STORAGE_OK);
    EXPECT(udb_storage_sort_close(&sort),UDB_STORAGE_OK);
    UdbStorageNode node; EXPECT(udb_storage_node_get(e.cache,e.files,&state.blocks[0].catalog,&position,&node),UDB_STORAGE_OK);
    CHECK(node.kind==UDB_STORAGE_BRANCH && node.summary.count==5000 && node.summary.weight>UDB_STORAGE_LEAF_WEIGHT_MAX);
    EXPECT(udb_storage_journal_commit(&e.journal,&state),UDB_STORAGE_OK);
    EXPECT(udb_storage_engine_close(&e),UDB_STORAGE_OK);
    open_engine(&e,path,UDB_STORAGE_CACHE_DEFAULT);
    UdbStorageRead *old=NULL; EXPECT(udb_storage_read_open(&e,&old),UDB_STORAGE_OK);
    UdbStorageField out; EXPECT(udb_storage_read_field(old,&row,&out),UDB_STORAGE_OK);
    CHECK(out.kind==UDB_STORAGE_VALUE_STRING && strlen(out.string)==4096);
    mutate(&e,UDB_STORAGE_JOB_PREFIX,key('N',"entity",NULL,NULL),0); drain(&e);
    UdbStorageJob *job=next_job(&e,UDB_STORAGE_OK); UdbStorageRead *current=NULL;
    EXPECT(udb_storage_read_job(job,1,&current),UDB_STORAGE_OK);
    EXPECT(udb_storage_read_field(current,&row,&out),UDB_STORAGE_NOT_FOUND);
    result=udb_storage_node_get(e.cache,e.files,&current->generation->state.blocks[0].catalog,&position,&node);
    CHECK(result==UDB_STORAGE_NOT_FOUND || (result==UDB_STORAGE_OK && node.kind==UDB_STORAGE_LEAF && !node.summary.count));
    EXPECT(udb_storage_read_close(&current),UDB_STORAGE_OK); free_job(&e,job);
    EXPECT(udb_storage_reclaimer_refresh(&e),UDB_STORAGE_OK);
    /* Checkpoint/extent classification may not reclaim the held old snapshot. */
    EXPECT(udb_storage_read_field(old,&row,&out),UDB_STORAGE_OK); CHECK(strlen(out.string)==4096);
    EXPECT(udb_storage_read_close(&old),UDB_STORAGE_OK);
    CHECK(e.budget.peak[1]<=UDB_STORAGE_AUX_LIMIT); EXPECT(udb_storage_engine_close(&e),UDB_STORAGE_OK);
    open_engine(&e,path,256*1024*1024); UdbStorageRead *read=NULL;
    EXPECT(udb_storage_read_open(&e,&read),UDB_STORAGE_OK);
    EXPECT(udb_storage_read_field(read,&row,&out),UDB_STORAGE_NOT_FOUND);
    CHECK(!read->generation->state.blocks[0].summary.count && read->generation->state.source_sequence==2);
    EXPECT(udb_storage_read_close(&read),UDB_STORAGE_OK); EXPECT(udb_storage_engine_close(&e),UDB_STORAGE_OK);
}

static void cache_limits(const char *path)
{
    UdbStorageEngine e; open_engine(&e,path,UDB_STORAGE_CACHE_DEFAULT); drain(&e);
    UdbStorageFile file={0}; EXPECT(udb_storage_file_open(&e.directory,"stage_22222222222222222222222222222222_N_0.db",2,&file),UDB_STORAGE_OK);
    unsigned char bytes[65536]; UdbStoragePage page={0}; page.kind=UDB_STORAGE_PAGE_METADATA;
    page.object=99; page.generation=1; UdbStorageCacheEntry *entries[320];
    for(unsigned int i=0;i<320;i++) {
        page.ordinal=i; EXPECT(udb_storage_page_encode(&page,bytes,sizeof(bytes)),UDB_STORAGE_OK);
        EXPECT(udb_storage_file_write(&file,(uint64_t)i*65536,bytes,sizeof(bytes)),UDB_STORAGE_OK);
        EXPECT(udb_storage_cache_acquire(e.cache,&file,(uint64_t)i*65536,1,entries+i),UDB_STORAGE_OK);
    }
    EXPECT(udb_storage_cache_resize(e.cache,16*1024*1024),UDB_STORAGE_BUSY);
    CHECK(e.budget.limit[0]==UDB_STORAGE_CACHE_DEFAULT);
    EXPECT(udb_storage_file_write(&file,0,bytes,sizeof(bytes)),UDB_STORAGE_BUSY);
    for(unsigned int i=0;i<320;i++) EXPECT(udb_storage_cache_release(e.cache,entries[i]),UDB_STORAGE_OK);
    EXPECT(udb_storage_cache_resize(e.cache,16*1024*1024),UDB_STORAGE_OK);
    CHECK(e.budget.limit[0]==16*1024*1024 && e.budget.used[0]<=e.budget.limit[0]);
    EXPECT(udb_storage_cache_resize(e.cache,512*1024*1024),UDB_STORAGE_OK);
    page.ordinal=0; page.generation=2; EXPECT(udb_storage_page_encode(&page,bytes,sizeof(bytes)),UDB_STORAGE_OK);
    EXPECT(udb_storage_file_write(&file,0,bytes,sizeof(bytes)),UDB_STORAGE_OK);
    UdbStorageCacheEntry *entry=NULL;
    EXPECT(udb_storage_cache_acquire(e.cache,&file,0,1,&entry),UDB_STORAGE_CORRUPT);
    EXPECT(udb_storage_cache_acquire(e.cache,&file,0,2,&entry),UDB_STORAGE_OK);
    EXPECT(udb_storage_cache_release(e.cache,entry),UDB_STORAGE_OK);
    CHECK(!udb_storage_alloc(&e.budget,UDB_STORAGE_MEMORY_AUX,UDB_STORAGE_AUX_LIMIT));
    EXPECT(udb_storage_file_close(&file),UDB_STORAGE_OK); EXPECT(udb_storage_engine_close(&e),UDB_STORAGE_OK);
}

static void benchmark(const char *path, uint64_t count, size_t cache, int points)
{
    uint64_t start=udb_storage_monotonic_ms();
    UdbStorageEngine e; open_engine(&e,path,cache); drain(&e);
    UdbStorageSort *sort=NULL;
    EXPECT(udb_storage_sort_create(&e.directory,'N',count,UINT64_C(32)*1024*1024*1024,row_valid,NULL,&sort),UDB_STORAGE_OK);
    unsigned char bytes[UDB_STORAGE_RECORD_MAX]; size_t n; char name[40];
    for(uint64_t i=0;i<count;i++) {
        snprintf(name,sizeof(name),"u%010" PRIu64,i);
        UdbStorageRecord record={0}; record.operation=UDB_STORAGE_PUT; record.stamp.ordinal=(uint32_t)i;
        record.row=key('N',name,"vhost",NULL); record.row.kind=UDB_STORAGE_VALUE_STRING;
        record.row.value.data=(const unsigned char *)"x.test"; record.row.value.length=6;
        EXPECT(udb_storage_record_encode(&record,bytes,sizeof(bytes),&n),UDB_STORAGE_OK);
        EXPECT(udb_storage_sort_add(sort,bytes,n),UDB_STORAGE_OK);
    }
    EXPECT(udb_storage_sort_seal(sort),UDB_STORAGE_OK);
    UdbStorageResult result;
    do { result=udb_storage_sort_step(sort); } while(result==UDB_STORAGE_BUSY);
    EXPECT(result,UDB_STORAGE_OK);
    fprintf(stderr,"sorted rows=%" PRIu64 " ms=%" PRIu64 "\n",count,udb_storage_monotonic_ms()-start); fflush(stderr);
    UdbStorageSource source;
    EXPECT(udb_storage_sort_source(sort,1,0,&source),UDB_STORAGE_OK);
    UdbStorageState state=e.current->state; state.generation++; state.sequence=1;
    UdbStorageBlockRoot root={0}; root.block='N'; root.catalog.object=1; root.generation=state.generation; root.watermark=1;
    UdbStorageLogicalSummary summaries[4096];
    for(unsigned int i=0;i<4096;i++) {
        UdbStorageLogicalPosition position={0}; position.block='N'; position.partition=(uint16_t)i;
        uint64_t rows;
        EXPECT(udb_storage_sort_partition_count(sort,i,&rows),UDB_STORAGE_OK);
        if(!rows) { EXPECT(udb_storage_logical_empty(&position,summaries+i),UDB_STORAGE_OK); continue; }
        UdbStorageRebuild *rebuild=NULL;
        EXPECT(udb_storage_rebuild_create(e.cache,e.files,&root,&position,state.generation,1,state.next_object,&source,&rebuild),UDB_STORAGE_OK);
        EXPECT(udb_storage_rebuild_defer(rebuild),UDB_STORAGE_OK);
        do { result=udb_storage_rebuild_step(rebuild,&root,&state.next_object); } while(result==UDB_STORAGE_BUSY);
        EXPECT(result,UDB_STORAGE_OK); EXPECT(udb_storage_rebuild_summary(rebuild,summaries+i),UDB_STORAGE_OK);
        EXPECT(udb_storage_rebuild_close(&rebuild),UDB_STORAGE_OK);
    }
    EXPECT(udb_storage_logical_block('N',partition_one,summaries,&root.summary),UDB_STORAGE_OK);
    CHECK(root.summary.count==count); state.blocks[0]=root;
    EXPECT(udb_storage_sort_close(&sort),UDB_STORAGE_OK);
    EXPECT(udb_storage_journal_commit(&e.journal,&state),UDB_STORAGE_OK);
    uint64_t seed_ms=udb_storage_monotonic_ms()-start;
    size_t aux_peak=e.budget.peak[1], cache_peak=e.budget.peak[0];
    uint64_t disk=0;
    for(unsigned int i=0;i<9;i++) { uint64_t size; EXPECT(udb_storage_file_size(e.files+i,&size),UDB_STORAGE_OK); disk+=size; }
    EXPECT(udb_storage_engine_close(&e),UDB_STORAGE_OK);
    start=udb_storage_monotonic_ms(); open_engine(&e,path,cache); uint64_t recovery_ms=udb_storage_monotonic_ms()-start;
    UdbStorageRead *read=NULL; EXPECT(udb_storage_read_open(&e,&read),UDB_STORAGE_OK);
    const unsigned int samples=10000;
    uint64_t cold_ms,warm_ms;
    for(unsigned int pass=0;pass<2;pass++) {
        start=udb_storage_monotonic_ms();
        for(unsigned int i=0;i<samples;i++) {
            uint64_t item=((uint64_t)i*UINT64_C(2654435761))%count;
            snprintf(name,sizeof(name),"u%010" PRIu64,item); UdbStorageRow r=key('N',name,"vhost",NULL);
            UdbStorageField out; EXPECT(udb_storage_read_field(read,&r,&out),UDB_STORAGE_OK);
            CHECK(out.kind==UDB_STORAGE_VALUE_STRING && !strcmp(out.string,"x.test"));
        }
        if(!pass) cold_ms=udb_storage_monotonic_ms()-start; else warm_ms=udb_storage_monotonic_ms()-start;
    }
    uint64_t point_ms=0, point_growth=0, point_write_bytes=0;
    if(points) {
        atomic_store(&test_write_bytes,0); start=udb_storage_monotonic_ms(); bind_source(&e);
        for(unsigned int i=0;i<10;i++) {
            snprintf(name,sizeof(name),"u%010u",i);
            UdbStorageRow r=key('N',name,"vhost",NULL); r.kind=UDB_STORAGE_VALUE_STRING;
            r.value.data=(const unsigned char *)"updated.test"; r.value.length=12;
            mutate(&e,UDB_STORAGE_JOB_POINT,r,0);
        }
        drain(&e); point_ms=udb_storage_monotonic_ms()-start; point_write_bytes=atomic_load(&test_write_bytes);
        for(unsigned int i=0;i<11;i++) {
            UdbStorageJob *job=next_job(&e,UDB_STORAGE_OK);
            if(i==10) {
                UdbStorageRead *after=NULL; EXPECT(udb_storage_read_job(job,1,&after),UDB_STORAGE_OK);
                UdbStorageRow r=key('N',"u0000000009","vhost",NULL); UdbStorageField out;
                EXPECT(udb_storage_read_field(after,&r,&out),UDB_STORAGE_OK); CHECK(!strcmp(out.string,"updated.test"));
                EXPECT(udb_storage_read_close(&after),UDB_STORAGE_OK);
            }
            free_job(&e,job);
        }
        UdbStorageField old; UdbStorageRow r=key('N',"u0000000000","vhost",NULL);
        EXPECT(udb_storage_read_field(read,&r,&old),UDB_STORAGE_OK); CHECK(!strcmp(old.string,"x.test"));
        uint64_t size_after=0;
        for(unsigned int i=0;i<9;i++) { uint64_t size; EXPECT(udb_storage_file_size(e.files+i,&size),UDB_STORAGE_OK); size_after+=size; }
        CHECK(size_after>=disk); point_growth=size_after-disk;
    }
    if(e.budget.peak[1]>aux_peak) aux_peak=e.budget.peak[1];
    if(e.budget.peak[0]>cache_peak) cache_peak=e.budget.peak[0];
    EXPECT(udb_storage_read_close(&read),UDB_STORAGE_OK); EXPECT(udb_storage_engine_close(&e),UDB_STORAGE_OK);
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS memory = {sizeof(memory)};
    CHECK(GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)));
    uint64_t rss_peak_kib = (uint64_t)(memory.PeakWorkingSetSize / 1024);
#else
    struct rusage usage; CHECK(!getrusage(RUSAGE_SELF,&usage));
    uint64_t rss_peak_kib = (uint64_t)usage.ru_maxrss;
#endif
    printf("{\"rows\":%" PRIu64 ",\"cache_bytes\":%zu,\"seed_ms\":%" PRIu64 ",\"recovery_ms\":%" PRIu64
        ",\"samples\":%u,\"cold_lookup_ms\":%" PRIu64 ",\"warm_lookup_ms\":%" PRIu64
        ",\"aux_peak_bytes\":%zu,\"cache_peak_bytes\":%zu,\"disk_bytes\":%" PRIu64 ",\"rss_peak_kib\":%" PRIu64 ",\"point_transactions\":%u,\"point_batch_ms\":%" PRIu64 ",\"point_growth_bytes\":%" PRIu64 ",\"point_write_bytes\":%" PRIu64 "}\n",
        count,cache,seed_ms,recovery_ms,samples,cold_ms,warm_ms,aux_peak,cache_peak,disk,rss_peak_kib,points ? 10 : 0,point_ms,point_growth,point_write_bytes);
}
static int storage_driver_main(int argc, char **argv)
{
#ifdef _WIN32
	if (argc == 4 && !strcmp(argv[1], "win-hold"))
	{
		win_hold(argv[2], strtoul(argv[3], NULL, 10));
		return 0;
	}
#endif
    if((argc==5 || argc==6) && !strcmp(argv[1],"benchmark")) {
        uint64_t count=strtoull(argv[3],NULL,10),cache=strtoull(argv[4],NULL,10);
        CHECK(count && count<=10000000 && cache>=16*1024*1024 && cache<=SIZE_MAX/2);
        benchmark(argv[2],count,(size_t)cache,argc==6 && !strcmp(argv[5],"points")); return 0;
    }
    CHECK(argc==3);
    if (!strcmp(argv[1],"codec")) codec();
    else if (!strcmp(argv[1],"mutations")) mutations(argv[2]);
    else if (!strcmp(argv[1],"credits")) credits(argv[2]);
    else if (!strcmp(argv[1],"cancellation")) cancellation(argv[2]);
    else if (!strcmp(argv[1],"expiry")) expiry(argv[2]);
    else if (!strcmp(argv[1],"staging")) staging(argv[2],0,0);
    else if (!strcmp(argv[1],"bad-stage")) staging(argv[2],1,0);
    else if (!strcmp(argv[1],"abandoned")) staging(argv[2],0,1);
    else if (!strcmp(argv[1],"seed")) seed(argv[2],0,0);
    else if (!strcmp(argv[1],"crash")) seed(argv[2],0,1);
    else if (!strcmp(argv[1],"checkpoint")) seed(argv[2],1,0);
    else if (!strcmp(argv[1],"verify")) verify(argv[2],0);
    else if (!strcmp(argv[1],"corrupt")) verify(argv[2],1);
    else if (!strncmp(argv[1],"fault-",6)) io_fault(argv[2],atoi(argv[1]+6));
    else if (!strcmp(argv[1],"recover-old")) verify_fault(argv[2],42);
    else if (!strcmp(argv[1],"recover-new")) verify_fault(argv[2],43);
    else if (!strcmp(argv[1],"recover-either")) verify_fault(argv[2],0);
    else if (!strcmp(argv[1],"split-collapse-pins")) split_collapse_pins(argv[2]);
    else if (!strcmp(argv[1],"cache-limits")) cache_limits(argv[2]);
    else if (!strcmp(argv[1],"orphans")) orphans(argv[2]);
#ifdef _WIN32
	else if (!strcmp(argv[1], "win-offset")) win_offset(argv[2]);
	else if (!strcmp(argv[1], "win-insecure-directory")) win_insecure_directory(argv[2]);
	else if (!strcmp(argv[1], "win-insecure-file")) win_insecure_file(argv[2]);
	else if (!strcmp(argv[1], "win-hardlink")) win_hardlink(argv[2]);
	else if (!strcmp(argv[1], "win-close-cycles")) win_close_cycles(argv[2]);
	else if (!strcmp(argv[1], "win-reject-store")) win_reject_store(argv[2]);
	else if (!strcmp(argv[1], "win-lock-probe")) {
		UdbStorageEngine engine;
		UdbStorageResult result = win_try_open(argv[2], &engine);
		CHECK(result == UDB_STORAGE_BUSY || result == UDB_STORAGE_ERROR);
		puts("PASS");
		return 0;
	}
#endif
    else CHECK(0);
    puts("PASS"); return 0;
}

#ifdef _WIN32
int wmain(int argc, wchar_t **wide_argv)
{
	char **argv = calloc((size_t)argc + 1, sizeof(*argv));
	if (!argv)
		return 2;
	for (int i = 0; i < argc; i++)
	{
		int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_argv[i], -1, NULL, 0, NULL, NULL);
		if (size <= 0 || !(argv[i] = malloc((size_t)size)) ||
			!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_argv[i], -1, argv[i], size, NULL, NULL))
		{
			for (int j = 0; j < argc; j++)
				free(argv[j]);
			free(argv);
			return 2;
		}
	}
	int result = storage_driver_main(argc, argv);
	for (int i = 0; i < argc; i++)
		free(argv[i]);
	free(argv);
	return result;
}
#else
int main(int argc, char **argv)
{
	return storage_driver_main(argc, argv);
}
#endif
