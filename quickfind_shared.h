////////////////////////////////////////////////////////////////
// rune: Scalars

typedef int8_t      i8;
typedef int16_t     i16;
typedef int32_t     i32;
typedef int64_t     i64;

typedef uint8_t     u8;
typedef uint16_t    u16;
typedef uint32_t    u32;
typedef uint64_t    u64;

typedef size_t      usize;

typedef float       f32;
typedef double      f64;

typedef wchar_t     wchar;

////////////////////////////////////////////////////////////////
// rune: Macros

#define null 0

#define KILOBYTES(x) ((x) * 1024LL)
#define MEGABYTES(x) (KILOBYTES(x) * 1024LL)
#define GIGABYTES(x) (MEGABYTES(x) * 1024LL)

#define countof(a) (sizeof(a) / sizeof(*(a)))

#define STRINGIFY_(a) #a
#define STRINGIFY(x) STRINGIFY_(x)
#define LOCATION (__FILE__ "(" STRINGIFY(__LINE__) ")")
#define COALESCE(a, b) ((a)?(a):(b))

////////////////////////////////////////////////////////////////
// rune: ANSI console colors

#define ANSI_HOME                   "\033[H"
#define ANSI_ERASE_SCREEN           "\033[2J"

#define ANSI_RESET                  "\033[0m"

#define ANSI_FG_DEFAULT             "\033[39m"
#define ANSI_FG_BLACK               "\033[30m"
#define ANSI_FG_WHITE               "\033[97m"
#define ANSI_FG_DARK_RED            "\033[31m"
#define ANSI_FG_DARK_GREEN          "\033[32m"
#define ANSI_FG_DARK_YELLOW         "\033[33m"
#define ANSI_FG_DARK_BLUE           "\033[34m"
#define ANSI_FG_DARK_MAGENTA        "\033[35m"
#define ANSI_FG_DARK_CYAN           "\033[36m"
#define ANSI_FG_DARK_GRAY           "\033[90m"
#define ANSI_FG_GRAY                "\033[37m"
#define ANSI_FG_RED                 "\033[91m"
#define ANSI_FG_GREEN               "\033[92m"
#define ANSI_FG_YELLOW              "\033[93m"
#define ANSI_FG_BLUE                "\033[94m"
#define ANSI_FG_MAGENTA             "\033[95m"
#define ANSI_FG_CYAN                "\033[96m"

#define ANSI_BG_DEFAULT             "\033[49m"
#define ANSI_BG_BLACK               "\033[40m"
#define ANSI_BG_WHITE               "\033[47m"
#define ANSI_BG_DARK_RED            "\033[41m"
#define ANSI_BG_DARK_GREEN          "\033[42m"
#define ANSI_BG_DARK_YELLOW         "\033[43m"
#define ANSI_BG_DARK_BLUE           "\033[44m"
#define ANSI_BG_DARK_MAGENTA        "\033[45m"
#define ANSI_BG_DARK_CYAN           "\033[46m"
#define ANSI_BG_DARK_GRAY           "\033[100m"
#define ANSI_BG_GRAY                "\033[107m"
#define ANSI_BG_RED                 "\033[101m"
#define ANSI_BG_GREEN               "\033[102m"
#define ANSI_BG_YELLOW              "\033[103m"
#define ANSI_BG_BLUE                "\033[104m"
#define ANSI_BG_MAGENTA             "\033[105m"
#define ANSI_BG_CYAN                "\033[106m"

////////////////////////////////////////////////////////////////
// rune: Debug log

typedef enum log_level {
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_ERROR,

    LOG_LEVEL_COUNT
} log_level;

#define debug_log_info(format, ...)         debug_log(LOG_LEVEL_INFO,    __FILE__, __LINE__, format, __VA_ARGS__ )
#define debug_log_warning(format, ...)      debug_log(LOG_LEVEL_WARNING, __FILE__, __LINE__, format, __VA_ARGS__ )
#define debug_log_error(format, ...)        debug_log(LOG_LEVEL_ERROR,   __FILE__, __LINE__, format, __VA_ARGS__ )
#define debug_log_error_win32(function)     debug_log_error(function " failed (%i).", GetLastError())

static void debug_log(log_level level, char *filename, u32 linenumber, _Printf_format_string_ char *format, ...);
static void debug_log_va(log_level level, char *filename, u32 linenumber, char *format, va_list args);

////////////////////////////////////////////////////////////////
// rune: Pointer artithmetic

static inline void *ptr_add(void *ptr, usize offset);

////////////////////////////////////////////////////////////////
// rune: Pipe messages

#pragma warning ( disable : 4200 ) // C4200: nonstandard extension used: zero-sized array in struct/union

#define QUICKFIND_PIPE_NAME "\\\\.\\pipe\\QuickFind"

typedef enum msg_type msg_type;
enum msg_type {
    MSG_TYPE_NONE,
    MSG_TYPE_QUERY_REQUEST,      // msg_query_request
    MSG_TYPE_QUERY_RESPONSE,     // msg_query_response
};

typedef struct msg_query_request msg_query_request;
struct msg_query_request {
    u32 return_count;     // NOTE(rune): Number of results to return
    u64 skip_count;       // NOTE(rune): Number of results to skip before beginning to return results. Useful for pagination or scrolling lists.
    u64 stop_count;       // NOTE(rune): Run query until stop_count number of results is found.
    quickfind_flags flags;
};

typedef struct msg_query_response msg_query_response;
struct msg_query_response {
    u64 found_count;
    u32 return_count;
};

// NOTE(rune): Variably sized struct, total size is sizeof(query_result_item_t) + path_size
typedef struct query_result_item query_result_item;
struct query_result_item {
    u64 id;
    u32 attributes;

    // NOTE(rune): Including null terminator
    u32 path_size;
    char path[];
};

typedef struct msg msg;
struct msg {
    struct {
        msg_type type;
        quickfind_error error;
        union {
            msg_query_request query_request;
            msg_query_response query_response;
        };

        u32 body_size;
    } head;

    u8 body[MEGABYTES(1)];
};

////////////////////////////////////////////////////////////////
// rune: Pipe read/write

static quickfind_error pipe_write_msg(HANDLE pipe, msg *msg);
static quickfind_error pipe_read_msg(HANDLE pipe, msg *msg);

////////////////////////////////////////////////////////////////
// rune: Zeroing

#define zero_struct(a)          (memset(a, 0, sizeof(*a)))
#define zero_struct_array(a,c)  (memset(a, 0, sizeof(*a) * c))
