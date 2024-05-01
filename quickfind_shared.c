////////////////////////////////////////////////////////////////
// rune: Debug log

static void debug_log_va(log_level level, char *filename, u32 linenumber, char *format, va_list args) {
    char buffer[1024];

    u32 length = snprintf(buffer, sizeof(buffer), "%s(%i): ", filename, linenumber);
    if (length < sizeof(buffer)) {
        vsnprintf(buffer + length, sizeof(buffer) - length, format, args);
    }

    // TODO(rune): Log to a file instead of console

    switch (level) {
        case LOG_LEVEL_INFO: {
            printf(ANSI_FG_CYAN "%s\n" ANSI_RESET, buffer);
        } break;

        case LOG_LEVEL_WARNING: {
            printf(ANSI_FG_YELLOW "%s\n" ANSI_RESET, buffer);
        } break;

        case LOG_LEVEL_ERROR: {
            printf(ANSI_FG_RED "%s\n" ANSI_RESET, buffer);
        } break;
    }
}

static void debug_log(log_level level, char *filename, u32 linenumber, _Printf_format_string_ char *format, ...) {
    va_list args;
    va_start(args, format);
    debug_log_va(level, filename, linenumber, format, args);
    va_end(args);
}

////////////////////////////////////////////////////////////////
// rune: Pointer artithmetic

static inline void *ptr_add(void *ptr, usize offset) {
    return (u8 *)ptr + offset;
}

////////////////////////////////////////////////////////////////
// rune: Pipe read/write

static quickfind_error pipe_write_msg(HANDLE pipe, msg *msg) {
    u32 msg_size = sizeof(msg->head) + msg->head.body_size;
    DWORD bytes_written = 0;
    if (!WriteFile(pipe, msg, msg_size, &bytes_written, null)) {
        debug_log_error_win32("WriteFile");
        return QUICKFIND_ERROR_IO_WRITE;
    }

    if (bytes_written != msg_size) {
        debug_log_error("Size written to pipe (%u bytes) does not match size of message (%u bytes).", bytes_written, msg_size);
        return QUICKFIND_ERROR_IO_WRITE;
    }

    return QUICKFIND_OK;
}


static quickfind_error pipe_read_msg(HANDLE pipe, msg *msg) {
    DWORD bytes_read = 0;
    if (!ReadFile(pipe, msg, sizeof(*msg), &bytes_read, null)) {
        debug_log_error_win32("ReadFile");
        return QUICKFIND_ERROR_IO_READ;
    }

    if (bytes_read < sizeof(msg->head)) {
        debug_log_error("Size read from pipe (%u bytes) is less than message header size (%u bytes).", bytes_read, sizeof(msg->head));
        return QUICKFIND_ERROR_IO_READ;
    }

    if (msg->head.body_size > sizeof(msg->body)) {
        debug_log_error("Body size in header (%u bytes) was larger than max allowed body size (%u bytes).", msg->head.body_size, sizeof(msg->body));
    }

    return QUICKFIND_OK;
}
