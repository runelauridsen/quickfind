#ifndef QUICKFIND_CLIENT_H
#define QUICKFIND_CLIENT_H

// NOTE(rune): This is the public API for the quickfind client

#include <stdint.h>
#include <stdbool.h>

////////////////////////////////////////////////////////////////
// rune: Macros

#ifdef QUICKFIND_API_EXPORT
#   define QUICKFIND_API __declspec(dllexport)
#endif

#ifdef QUICKFIND_API_IMPORT
#   define QUICKFIND_API __declspec(dllimport)
#endif

#ifdef QUICKFIND_API_STATIC
#   define QUICKFIND_API static
#endif

////////////////////////////////////////////////////////////////
// rune: Types

typedef enum quickfind_error {
    QUICKFIND_OK,                                   // No error
    QUICKFIND_ERROR_CANCELLED,                      // Query was cancelled
    QUICKFIND_ERROR_INVALID_REQUEST,                // Client sent an invalid request to server
    QUICKFIND_ERROR_INVALID_RESPONSE,               // Server send an invalid response to client
    QUICKFIND_ERROR_SERVER_NOT_INITIALIZED,         // Server is not yet initialized, or encountered an error while initializing.
    QUICKFIND_ERROR_DATABASE_NOT_INITIALIZED,       // Database is not yet initialized, or encountered an error while initializing.
    QUICKFIND_ERROR_COULD_NOT_CONNECT_TO_SERVER,    // Could not open named pipe instance to server
    QUICKFIND_ERROR_OUT_OF_MEMORY,                  // Memory allocation failed
    QUICKFIND_ERROR_WIN32,                          // Win32 call returned an unexpected error code
    QUICKFIND_ERROR_COULD_NOT_GET_PATH,             // Server could not form path to database file
    QUICKFIND_ERROR_CONNECTION_TIMEOUT,             // Waiting for named pipe instance timed out
    QUICKFIND_ERROR_IO_READ,                        // Error during a file read operation
    QUICKFIND_ERROR_IO_WRITE,                       // Error during a file write operation

    QUICKFIND_ERROR_NO_RESULT                       // Result set has iterated through all returned results, or has not begun iterating yet.
} quickfind_error;

typedef enum quickfind_flags {
    QUICKFIND_FLAG_NORMAL           = 0x0,
    QUICKFIND_FLAG_CASE_SENSITIVE   = 0x1,
    QUICKFIND_FLAG_FULLNAME         = 0x2,
    QUICKFIND_FLAG_ONLY_FILES       = 0x4,
    QUICKFIND_FLAG_ONLY_DIRECTORIES = 0x8
} quickfind_flags;

typedef struct quickfind_params quickfind_params;
struct quickfind_params {
    char    *text;
    uint32_t text_length;

    quickfind_flags flags;

    uint32_t return_count;
    uint64_t skip_count;
    uint64_t stop_count;
};

typedef struct quickfind_results quickfind_results;
struct quickfind_results {
    // NOTE(rune): Opaque data used by the quickfind_x functions.
    uint32_t                  current_item_index;
    struct query_result_item *current_item;
    struct msg               *msg;
};

////////////////////////////////////////////////////////////////
// rune: Functions

QUICKFIND_API quickfind_error     quickfind_open(quickfind_params *params, quickfind_results *results, uint32_t connection_timout_millis, bool give_way_to_next_thread);
QUICKFIND_API void                quickfind_close(quickfind_results *results);
QUICKFIND_API bool                quickfind_next(quickfind_results *results);
QUICKFIND_API uint32_t            quickfind_get_return_count(quickfind_results *results);
QUICKFIND_API uint64_t            quickfind_get_found_count(quickfind_results *results);
QUICKFIND_API char *              quickfind_get_result_full_path(quickfind_results *results);
QUICKFIND_API uint32_t            quickfind_get_result_attributes(quickfind_results *results);
QUICKFIND_API uint64_t            quickfind_get_result_id(quickfind_results *results);

#endif
