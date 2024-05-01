////////////////////////////////////////////////////////////////
// rune: Heap

static void *heap_alloc(usize size, bool init_to_zero);
static void *heap_realloc(void *mem, usize size, bool init_to_zero);
static void  heap_free(void *mem);

////////////////////////////////////////////////////////////////
// rune: Heap tracking

#define TRACK_ALLOCATIONS 1
#define PRINT_ALLOCATIONS 0

typedef struct tracked_allocation tracked_allocation;
struct tracked_allocation {
    void *ptr;
    usize size;

    char *location_allocated;
    char *location_reallocated;
    char *location_freed;

    u32  reallocation_count;
    bool occupied;
};

static tracked_allocation g_tracked_allocations[1024];

static tracked_allocation * find_tracked_allocation_slot(void *ptr);
static void *               tracked_heap_alloc(usize size, bool init_to_zero, char *caller_location);
static void *               tracked_heap_realloc(void *ptr, usize size, bool init_to_zero, char *caller_location);
static void                 tracked_heap_free(void *ptr, char *caller_location);
static void                 print_tracked_allocations(bool print_summary, bool print_individual);

////////////////////////////////////////////////////////////////
// rune: Dynamic array

// NOTE(rune): elem_size is not strictly necessary, since elem_size is passed as argument
// to all dynbuffer_void_x functions anyway, buts it nice to have elem_size as a field
// when serializing/deserializing dynamic arrays,
#define ARRAY_MEMBERS(T) T *elems; usize count, count_allocated, elem_size;

typedef struct array array;
struct array {
    ARRAY_MEMBERS(void)
};

#define array(T) array_##T

#define TYPEDEF_ARRAY(T)             \
typedef union array(T) array(T);     \
union array(T) {                     \
    struct { ARRAY_MEMBERS(T) };     \
    struct array as_void;            \
}

#define array_create(array, initial_capacity, init_to_zero)      array_void_create     (&(array)->as_void, sizeof(*(array)->elems), initial_capacity, init_to_zero)
#define array_create_size(array, initial_size, init_to_zero)     array_void_create_size(&(array)->as_void, sizeof(*(array)->elems), initial_size, init_to_zero)
#define array_reserve(array, reserve_count, init_to_zero)        array_void_reserve    (&(array)->as_void, sizeof(*(array)->elems), reserve_count, init_to_zero)
#define array_push_count(array, push_count, init_to_zero)        array_void_push_count (&(array)->as_void, sizeof(*(array)->elems), push_count, init_to_zero)
#define array_push(array, init_to_zero)                          array_void_push       (&(array)->as_void, sizeof(*(array)->elems), init_to_zero)
#define array_destroy(array)                                     array_void_destroy    (&(array)->as_void)

static bool  array_void_create(array *array, usize elem_size, usize initial_capacity, bool init_to_zero);
static bool  array_void_create_size(array *array, usize elem_size, usize initial_size, bool init_to_zero);
static void  array_void_destroy(array *array);
static bool  array_void_reserve(array *array, usize elem_size, usize reserve_count, bool init_to_zero);
static void *array_void_push_count(array *array, usize elem_size, usize push_count, bool init_to_zero);
static void *array_void_push(array *array, usize elem_size, bool init_to_zero);

////////////////////////////////////////////////////////////////
// rune: Fixed sized buffer

typedef struct buffer buffer;
struct buffer {
    u8 *data;
    u64 size;
    u64 capacity;
};

static void *buffer_append(buffer *b, u64 size);
static void buffer_reset(buffer *b);

////////////////////////////////////////////////////////////////
// rune: SIMD

// Reference: https://www.officedaytime.com/simd512e/
// Reference: http://0x80.pl/articles/simd-strfind.html

static char *simd_memmem_count_zeroes(char *s, usize n, char *needle, usize k, usize *zero_count);
static char *simd_memmem_count_zeroes_nocase(char *s, usize n, char *needle, usize k, usize *zero_count);
static char *simd_memchr_count_zeroes(char *s, usize n, char c, usize *zero_count);
static char *simd_memchr_count_zeroes_nocase(char *s, usize n, char c, usize *zero_count);

////////////////////////////////////////////////////////////////
// rune: File IO

typedef struct file file;
struct file {
    HANDLE handle;
    bool ok;
};

typedef enum file_access file_access;
enum file_access {
    FILE_ACCESS_WRITE,
    FILE_ACCESS_READ,
};

static void file_open(file *file, char *path, file_access access);
static void file_close(file *file);

static void file_read(file *file, void *buffer, usize size);
static void file_read_u8(file *file, u8 *buffer);
static void file_read_u16(file *file, u16 *buffer);
static void file_read_u32(file *file, u32 *buffer);
static void file_read_u64(file *file, u64 *buffer);
static void file_read_usize(file *file, usize *buffer);
static void file_read_array(file *file, array *array);

static void file_write(file *file, void *buffer, usize size);
static void file_write_u8(file *file, u8 buffer);
static void file_write_u16(file *file, u16 buffer);
static void file_write_u32(file *file, u32 buffer);
static void file_write_u64(file *file, u64 buffer);
static void file_write_usize(file *file, usize buffer);
static void file_write_array(file *file, array array);

////////////////////////////////////////////////////////////////
// rune: Database

#define FILE_ATTRIBUTE_NOT_IN_USE (1 << 31)

// NOTE(rune): Same file reference type as the NTFS Master File Table uses.
typedef struct record_id record_id;
struct record_id {
    union {
        struct {
            u64 record_number : 48;
            u64 sequence_number : 16;
        };
        u64 id64;
    };
};

typedef struct record record;
struct record {
    usize name_offset;
    u32 attributes;

    record_id id;
    record_id parent_id;
};

typedef enum change_type change_type;
enum change_type {
    CHANGE_TYPE_INSERT,
    CHANGE_TYPE_UPDATE,
    CHANGE_TYPE_DELETE
};

typedef struct change change;
struct change {
    u64 usn;

    change_type type;
    record_id id;
    record_id parent_id;

    wchar *wname;
    u32 wname_length;
    u32 attributes;
    bool ignore;

    change *next;
    change *prev;
};

typedef struct change_list change_list;
struct change_list {
    change *first;
    change *last;
};

TYPEDEF_ARRAY(record);
TYPEDEF_ARRAY(u32);
TYPEDEF_ARRAY(char);

typedef struct db db;
struct db {
    // rune: All file/directory names in null terminated utf8
    array(char) name_buffer;

    // rune: Stored in same order as name_buffer
    array(record) record_array;

    // rune: Array of u32s. Index with record_number to get index of the new_record
    // in the record_array, with the most recent sequence_number for that record_number.
    array(u32) lookup_array;

    u64 latest_usn;
    u64 latest_journal_id;
    u32 records_not_in_use_count;
};

static void         db_create(db *db);
static bool         db_create_from_file(db *db, char *file_path);
static void         db_destroy(db *db);
static bool         db_write_to_file(db *db, char *file_path);

static record *     db_get_record_by_id(db *db, record_id id);
static record *     db_get_record_parent(db *db, record *record);
static char *       db_get_record_name(db *db, record *record);

static record *     db_insert(db *db, record_id id, record_id parent_id, u32 attributes, wchar *wname, u32 wname_len);
static record *     db_update(db *db, record_id id, record_id parent_id, u32 attributes, wchar *wname, u32 wname_len);
static void         db_delete(db *db, record_id id);
static void         db_apply_changes(db *db, change_list changes);
static uint32_t     db_prune(db *db);

////////////////////////////////////////////////////////////////
// rune: Sanity checks

static bool debug_sanity_check_names(db *db);
static bool debug_sanity_check_lookup(db *db);

////////////////////////////////////////////////////////////////
// rune: Query

typedef struct query_result query_result;
struct query_result {
    quickfind_error error;
    u64 found_count;
    u32 return_count;
};

// TODO(rune): Cleanup. This seems to be much more complicated than it needs to.
static bool walk_ancestors_is_child_of_root(
    record  *record,
    db      *database,
    u32      max_depth
);

// TODO(rune): Cleanup. This seems to be much more complicated than it needs to.
static u32 walk_ancestors_build_path(
    record  *child,
    db      *database,
    record **ancestor_buffer,
    u32      ancestor_buffer_count,
    char    *path_buffer,
    usize    path_buffer_size
);

// TODO(rune): Cleanup. This seems to be much more complicated than it needs to.
static bool matches_query_flags(
    record          *record,
    quickfind_flags  flags,
    char            *query,
    usize            query_len,
    char            *match,
    usize            match_len
);

// TODO(rune): Cleanup. This seems to be much more complicated than it needs to.
static char *find_first_occurrence_and_count_nulls(
    char            *buffer,
    usize            buffer_length,
    char            *look_for,
    usize            look_for_length,
    quickfind_flags  flags,
    usize           *null_count
);

// NOTE(rune): query_result_item_t's are pushed to result_buffer.
static query_result run_query(quickfind_params params, buffer *result_buffer, db *database);

////////////////////////////////////////////////////////////////
// rune: Server

#define REQUEST_BUFFER_SIZE     KILOBYTES(1)
#define RESPONSE_BUFFER_SIZE    MEGABYTES(1)

typedef struct server server;
struct server {
    bool        shutdown;

    db          database;
    SRWLOCK     database_lock;
    bool        database_initialized;
    char        database_path[MAX_PATH];

    HANDLE      pipe;
    HANDLE      connection_event;
    HANDLE      shutdown_event;
    HANDLE      worker_thread;

    msg request;
    msg response;

    u8 usn_query_storage[MEGABYTES(1)];
};

// rune: Mutex
static void server_acquire_write_lock(server *server);
static void server_release_write_lock(server *server);
static void server_acquire_read_lock(server *server);
static void server_release_read_lock(server *server);

// rune: Worker thread
static DWORD WINAPI server_worker_thread_proc(LPVOID lpParameter);

// rune: Setup
static bool   server_get_database_file_path(char *buffer, usize buffer_size);
static HANDLE server_create_event(BOOL manual_reset);
static HANDLE server_create_thread(LPTHREAD_START_ROUTINE start, void *param);
static HANDLE server_create_pipe();

// rune: Requets
static void   server_calculate_response(server *server);

// rune: Lifetime
static bool server_create(server *server);
static void server_destroy(server *server);
static bool server_run(server *server);
static void server_begin_shutdown(server *server);
