////////////////////////////////////////////////////////////////
// rune: Globals

static volatile u32 quickfind_g_query_inc = 0;

////////////////////////////////////////////////////////////////
// rune: Internal functions

static void *quickfind__alloc(usize size) {
    return HeapAlloc(GetProcessHeap(), 0, size);
}

static void quickfind__free(void *p) {
    HeapFree(GetProcessHeap(), 0, p);
}

static bool quickfind__has_valid_item(quickfind_results *results) {
    if (!results) {
        return false;
    }

    if (results->current_item == null) {
        return false;
    }

    if (results->current_item_index == -1) {
        return false;
    }

    if (results->current_item_index >= results->msg->head.query_response.return_count) {
        return false;
    }

    uint8_t *item_end_ptr = (uint8_t *)(results->current_item + 1) + results->current_item->path_size;
    uint32_t item_end_idx = (uint32_t)(item_end_ptr - (uint8_t *)results->msg->body);
    if (item_end_idx > results->msg->head.body_size) {
        return false;
    }

    return true;
}

////////////////////////////////////////////////////////////////
// rune: Public API

QUICKFIND_API quickfind_error quickfind_open(quickfind_params *params, quickfind_results *r, uint32_t connection_timeout_millis, bool yield_to_next_thread) {
    quickfind_error error = QUICKFIND_OK;
    memset(r, 0, sizeof(*r));

    u32 query_inc_begin = InterlockedIncrement(&quickfind_g_query_inc);

    // rune: If connection_timeout_millis is not specified, wait indefinitely.
    if (connection_timeout_millis == 0) {
        connection_timeout_millis = INFINITE;
    }

    // rune: If stop_count is not specified, stop when return_count is reached.
    if (params->stop_count == 0) {
        params->stop_count = params->skip_count + params->return_count;
    }

    ////////////////////////////////////////////////////////////////
    // rune: Wait for pipe connection or return an error.

    HANDLE pipe;
    {
        while (1) {
            // NOTE(rune): Check if another thread has called quickfind_open while this thread was waiting.
            if (!query_inc_begin != quickfind_g_query_inc && yield_to_next_thread) {
                return QUICKFIND_ERROR_CANCELLED;
            }

            pipe = CreateFileA(QUICKFIND_PIPE_NAME,
                               GENERIC_READ |  // read and write access
                               GENERIC_WRITE,
                               0,              // no sharing
                               null,           // default security attributes
                               OPEN_EXISTING,  // opens existing pipe
                               0,              // default attributes
                               null);          // no template file

            if (pipe != INVALID_HANDLE_VALUE) {
                break;
            }

            if (GetLastError() != ERROR_PIPE_BUSY) {
                return QUICKFIND_ERROR_COULD_NOT_CONNECT_TO_SERVER;
            }

            if (!WaitNamedPipeA(QUICKFIND_PIPE_NAME, connection_timeout_millis)) {
                return QUICKFIND_ERROR_CONNECTION_TIMEOUT;
            }
        }
    }

    ////////////////////////////////////////////////////////////////
    // Rune: Allocate, construct and senc request

    r->msg = quickfind__alloc(sizeof(msg));
    if (!r->msg) {
        error = QUICKFIND_ERROR_OUT_OF_MEMORY;
    }

    if (!error) {
        r->msg->head.type = MSG_TYPE_QUERY_REQUEST;
        r->msg->head.query_request.flags        = params->flags;
        r->msg->head.query_request.return_count = params->return_count;
        r->msg->head.query_request.skip_count   = params->skip_count;
        r->msg->head.query_request.stop_count   = params->stop_count;

        u32 body_size = min(sizeof(r->msg->body), params->text_length);
        r->msg->head.body_size = body_size;
        memcpy(r->msg->body, params->text, body_size);

        u32 bytes_written = 0;
        error = pipe_write_msg(pipe, r->msg);
    }

    ////////////////////////////////////////////////////////////////
    // rune: Recieve response from the quickfind service

    if (!error) {
        error = pipe_read_msg(pipe, r->msg);
    }

    if (!error) {
        if (r->msg->head.type != MSG_TYPE_QUERY_RESPONSE) {
            error = QUICKFIND_ERROR_INVALID_RESPONSE;
        }
    }

    ////////////////////////////////////////////////////////////////
    // rune: Cleanup

    CloseHandle(pipe);

    if (!error) {
        r->current_item = null;
        r->current_item_index = -1;
    } else {
        quickfind_close(r);
    }

    return error;
}

QUICKFIND_API void quickfind_close(quickfind_results *results) {
    if (results) {
        quickfind__free(results->msg);
    }
}

QUICKFIND_API uint32_t quickfind_get_return_count(quickfind_results *results) {
    uint32_t ret = 0;
    if (results) {
        ret = results->msg->head.query_response.return_count;
    }
    return ret;
}

QUICKFIND_API uint64_t quickfind_get_found_count(quickfind_results *results) {
    uint64_t ret = 0;
    if (results) {
        ret = results->msg->head.query_response.found_count;
    }
    return ret;
}

QUICKFIND_API bool quickfind_next(quickfind_results *r) {
    if (!r) {
        return false;
    }

    if (r->msg->head.query_response.return_count == 0) {
        return false;
    }

    // NOTE(rune): results->current_item is -1 the first time quickfind_advance is called.
    if (r->current_item_index == -1) {
        r->current_item = (query_result_item *)r->msg->body;
        r->current_item_index = 0;
        return true;
    }

    if (r->current_item_index + 1 < r->msg->head.query_response.return_count) {
        u32 current_result_size = sizeof(*r->current_item) + r->current_item->path_size;
        query_result_item *next_result = ptr_add(r->current_item, current_result_size);
        query_result_item *data_end    = ptr_add(&r->msg->body, r->msg->head.body_size);

        if (next_result < data_end) {
            r->current_item = next_result;
            r->current_item_index++;
            bool ret = quickfind__has_valid_item(r);
            return ret;
        }
    }

    // NOTE(rune): No more results
    r->current_item = null;
    r->current_item_index = r->msg->head.query_response.return_count;
    return false;
}

QUICKFIND_API char *quickfind_get_result_full_path(quickfind_results *results) {
    char *ret = "";
    if (quickfind__has_valid_item(results)) {
        ret = results->current_item->path;
    }
    return ret;
}

QUICKFIND_API uint32_t quickfind_get_result_attributes(quickfind_results *results) {
    uint32_t ret = 0;
    if (quickfind__has_valid_item(results)) {
        ret = results->current_item->attributes;
    }
    return ret;
}

QUICKFIND_API uint64_t quickfind_get_result_id(quickfind_results *results) {
    uint64_t ret = 0;
    if (quickfind__has_valid_item(results)) {
        ret = results->current_item->id;
    }
    return ret;
}
