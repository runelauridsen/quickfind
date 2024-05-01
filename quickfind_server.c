////////////////////////////////////////////////////////////////
// rune: Heap

static void *heap_alloc(usize size, bool init_to_zero) {
    return HeapAlloc(GetProcessHeap(), init_to_zero ? HEAP_ZERO_MEMORY : 0, size);
}

static void *heap_realloc(void *mem, usize size, bool init_to_zero) {
    return HeapReAlloc(GetProcessHeap(), init_to_zero ? HEAP_ZERO_MEMORY : 0, mem, size);
}

static void heap_free(void *mem) {
    HeapFree(GetProcessHeap(), 0, mem);
}

////////////////////////////////////////////////////////////////
// rune: Heap tracking

// TODO(rune): This is not thread safe
static tracked_allocation *find_tracked_allocation_slot(void *ptr) { // TODO(rune): This is not thread safe
    tracked_allocation *result = null;

    if (ptr) {
        for (u32 i = 0; i < countof(g_tracked_allocations); i++) {
            if (g_tracked_allocations[i].ptr == ptr) {
                result = &g_tracked_allocations[i];
                break;
            }
        }
    } else {
        for (u32 i = 0; i < countof(g_tracked_allocations); i++) {
            if (g_tracked_allocations[i].occupied == false) {
                result = &g_tracked_allocations[i];
                break;
            }
        }
    }

    return result;
}

static void *tracked_heap_alloc(usize size, bool init_to_zero, char *caller_location) {
    void *ptr = heap_alloc(size, init_to_zero);

#if PRINT_ALLOCATIONS
    printf(ANSI_FG_CYAN "-- Heap allocation --\n" ANSI_RESET);
    printf("Address:                %p\n", ptr);
    printf("Size:                   %zx\n", size);
    printf("Location allocated:     %s\n", caller_location);
    printf("\n");
#endif

    tracked_allocation *slot = find_tracked_allocation_slot(null);
    assert(slot && "Not enough tracked allocation slots.");
    zero_struct(slot);

    slot->ptr                = ptr;
    slot->size               = size;
    slot->location_allocated = caller_location;
    slot->occupied           = true;

    return ptr;
}

static void *tracked_heap_realloc(void *ptr, usize size, bool init_to_zero, char *caller_location) {
    void *new_ptr = heap_realloc(ptr, size, init_to_zero);

    tracked_allocation *slot = find_tracked_allocation_slot(ptr);
    assert(slot && "Bad heap reallocate pointer.");

#if PRINT_ALLOCATIONS
    printf(ANSI_FG_BLUE "-- Heap reallocation --\n" ANSI_RESET);
    printf("Address:                %p -> %p\n", ptr, new_ptr);
    printf("Size:                   %zx -> %zx\n", slot->size, size);
    printf("Location allocated:     %s\n", caller_location);
    printf("Location reallocated:   %s\n", caller_location);
    printf("Reallocation count:     %i\n", slot->reallocation_count);
    printf("\n");
#endif

    slot->ptr                  = new_ptr;
    slot->size                 = size;
    slot->location_reallocated = caller_location;
    slot->reallocation_count++;

    return new_ptr;
}

static void tracked_heap_free(void *ptr, char *caller_location) {
    heap_free(ptr);

    tracked_allocation *slot = find_tracked_allocation_slot(ptr);
    assert(slot && "Bad heap free pointer.");

    slot->location_freed = caller_location;
    slot->occupied = false;

#if PRINT_ALLOCATIONS
    printf(ANSI_FG_GREEN "-- Heap free --\n" ANSI_RESET);
    printf("Address:                %p\n", slot->ptr);
    printf("Size:                   %zx\n", slot->size);
    printf("Reallocation count:     %i\n", slot->reallocation_count);
    printf("Location allocated:     %s\n", COALESCE(slot->location_allocated, ""));
    printf("Location reallocated:   %s\n", COALESCE(slot->location_reallocated, ""));
    printf("Location freed:         %s\n", COALESCE(slot->location_freed, ""));
    printf("\n");
#endif

}

static void print_tracked_allocations(bool print_summary, bool print_individual) {
    if (print_summary) {
        size_t total_count = 0;
        size_t total_size  = 0;
        for (u32 i = 0; i < countof(g_tracked_allocations); i++) {
            tracked_allocation *t = &g_tracked_allocations[i];
            if (t->occupied) {
                total_count++;
                total_size += t->size;
            }
        }

        printf(ANSI_FG_MAGENTA "-- Tracked heap allocations summary --\n" ANSI_RESET);
        printf("Total count:                   %zx\n", total_count);
        printf("Total size:                    %zx\n", total_size);
        printf("\n");
    }

    if (print_individual) {
        for (u32 i = 0; i < countof(g_tracked_allocations); i++) {
            tracked_allocation *t = &g_tracked_allocations[i];
            if (t->occupied) {
                printf(ANSI_FG_DARK_MAGENTA "-- Tracked heap allocation --\n" ANSI_RESET);
                printf("Address:                %p\n", t->ptr);
                printf("Size:                   %zx\n", t->size);
                printf("Reallocation count:     %i\n", t->reallocation_count);
                printf("Location allocated:     %s\n", COALESCE(t->location_allocated, ""));
                printf("Location reallocated:   %s\n", COALESCE(t->location_reallocated, ""));
                printf("Location freed:         %s\n", COALESCE(t->location_freed, ""));
                printf("\n");
            }
        }
    }
}

// NOTE(rune): Re-direct all following calls the memory tracking
#if TRACK_ALLOCATIONS
#define heap_alloc(size, init_to_zero)              tracked_heap_alloc(size, init_to_zero, LOCATION)
#define heap_reallocate(ptr, size, init_to_zero)    tracked_heap_realloc(ptr, size, init_to_zero, LOCATION)
#define heap_free(ptr)                              tracked_heap_free(ptr, LOCATION)
#endif

////////////////////////////////////////////////////////////////
// rune: Dynamic array

static bool array_void_create(array *array, usize elem_size, usize initial_capacity, bool init_to_zero) {
    assert(array);
    assert(initial_capacity);
    assert(elem_size);

    // TODO(rune): Error handling
    array->elems            = heap_alloc(initial_capacity * elem_size, init_to_zero);
    array->elem_size        = elem_size;
    array->count_allocated  = initial_capacity;
    array->count            = 0;
    return true;
}

static bool array_void_create_size(array *array, usize elem_size, usize initial_size, bool init_to_zero) {
    bool result = array_void_create(array, elem_size, initial_size / elem_size, init_to_zero);
    return result;
}

static void array_void_destroy(array *array) {
    if (array) {
        heap_free(array->elems);
        zero_struct(array);
    }
}

static bool array_void_reserve(array *array, usize elem_size, usize reserve_count, bool init_to_zero) {
    assert(array->elem_size == elem_size);

    bool result = false;

    if (array->count_allocated < reserve_count) {
        assert(array->count_allocated);

        usize new_count_allocated = array->count_allocated;
        while (new_count_allocated < reserve_count) {
            new_count_allocated *= 2;
        }

        void *new_elems = heap_realloc(array->elems, new_count_allocated * elem_size, init_to_zero);
        if (new_elems) {
            array->elems           = new_elems;
            array->count_allocated = new_count_allocated;

            result = true;
        }
    } else {
        result = true;
    }

    return result;
}

static void *array_void_push_count(array *array, usize elem_size, usize push_count, bool init_to_zero) {
    assert(array->elem_size == elem_size);

    void *result = null;

    if (array_void_reserve(array, elem_size, array->count + push_count, init_to_zero)) {
        result = (uint8_t *)array->elems + array->count * elem_size;
        array->count += push_count;
    }

    return result;
}

static void *array_void_push(array *array, usize elem_size, bool init_to_zero) {
    void *result = array_void_push_count(array, elem_size, 1, init_to_zero);
    return result;
}

////////////////////////////////////////////////////////////////
// rune: Fixed sized buffer

static void *buffer_append(buffer *b, u64 size) {
    void *ret = null;
    if (b->size + size <= b->capacity) {
        ret = b->data + b->size;
        b->size += size;
    }
    return ret;
}

static void buffer_reset(buffer *b) {
    b->size = 0;
}

////////////////////////////////////////////////////////////////
// rune: SIMD

static inline u32 count_trailing_zeroes(u32 value) {
    return _tzcnt_u32(value);
}

static inline u32 clear_leftmost_set(u32 value) {
    return value & (value - 1);
}

static inline u32 count_bits_set(u32 value) {
    return __popcnt(value);
}

static char *simd_memmem_count_zeroes(char *s, usize n, char *needle, usize k, usize *zero_count) {
    assert(k > 1);

    *zero_count = 0;

    __m256i first = _mm256_set1_epi8(needle[0]);
    __m256i last = _mm256_set1_epi8(needle[k - 1]);
    __m256i zero = _mm256_set1_epi8('\0');

    for (usize i = 0; i < n; i += 32) {
        __m256i block_first = _mm256_loadu_si256((__m256i *)(s + i));
        __m256i block_last = _mm256_loadu_si256((__m256i *)(s + i + k - 1));

        __m256i eq_first = _mm256_cmpeq_epi8(first, block_first);
        __m256i eq_last = _mm256_cmpeq_epi8(last, block_last);
        __m256i eq_zero = _mm256_cmpeq_epi8(zero, block_first);

        u32 mask_needle = _mm256_movemask_epi8(_mm256_and_si256(eq_first, eq_last));
        u32 mask_zero = _mm256_movemask_epi8(eq_zero);

        while (mask_needle != 0) {
            u32 bitpos = count_trailing_zeroes(mask_needle);

            if (memcmp(s + i + bitpos + 1, needle + 1, k - 2) == 0) {
                *zero_count += count_bits_set(mask_zero & ~(0xFFFFFFFF << bitpos));
                return s + i + bitpos;
            }

            mask_needle = clear_leftmost_set(mask_needle);
        }

        *zero_count += count_bits_set(mask_zero);
    }

    return null;
}

static char *simd_memmem_count_zeroes_nocase(char *s, usize n, char *needle, usize k, usize *zero_count) {
    assert(k > 1);

    *zero_count = 0;

    __m256i lower_first = _mm256_set1_epi8(tolower(needle[0]));
    __m256i lower_last = _mm256_set1_epi8(tolower(needle[k - 1]));

    __m256i upper_first = _mm256_set1_epi8(toupper(needle[0]));
    __m256i upper_last = _mm256_set1_epi8(toupper(needle[k - 1]));

    __m256i zero = _mm256_set1_epi8('\0');

    for (usize i = 0; i < n; i += 32) {
        __m256i block_first = _mm256_loadu_si256((__m256i *)(s + i));
        __m256i block_last = _mm256_loadu_si256((__m256i *)(s + i + k - 1));

        __m256i eq_lower_first = _mm256_cmpeq_epi8(lower_first, block_first);
        __m256i eq_lower_last = _mm256_cmpeq_epi8(lower_last, block_last);
        __m256i eq_upper_first = _mm256_cmpeq_epi8(upper_first, block_first);
        __m256i eq_upper_last = _mm256_cmpeq_epi8(upper_last, block_last);

        __m256i eq_first = _mm256_or_si256(eq_lower_first, eq_upper_first);
        __m256i eq_last = _mm256_or_si256(eq_lower_last, eq_upper_last);
        __m256i eq_zero = _mm256_cmpeq_epi8(zero, block_first);

        u32 mask_needle = _mm256_movemask_epi8(_mm256_and_si256(eq_first, eq_last));;
        u32 mask_zero = _mm256_movemask_epi8(eq_zero);

        while (mask_needle != 0) {
            u32 bitpos = count_trailing_zeroes(mask_needle);

            if (_memicmp(s + i + bitpos + 1, needle + 1, k - 2) == 0) {
                *zero_count += count_bits_set(mask_zero & ~(0xFFFFFFFF << bitpos));
                return s + i + bitpos;
            }

            mask_needle = clear_leftmost_set(mask_needle);
        }

        *zero_count += count_bits_set(mask_zero);
    }

    return null;
}

static char *simd_memchr_count_zeroes(char *s, usize n, char c, usize *zero_count) {
    *zero_count = 0;

    __m256i first = _mm256_set1_epi8(c);
    __m256i zero = _mm256_set1_epi8('\0');

    for (usize i = 0; i < n; i += 32) {
        __m256i block   = _mm256_loadu_si256((__m256i *)(s + i));
        __m256i eq      = _mm256_cmpeq_epi8(first, block);
        __m256i eq_zero = _mm256_cmpeq_epi8(zero, block);

        u32 mask_needle = _mm256_movemask_epi8(eq);
        u32 mask_zero = _mm256_movemask_epi8(eq_zero);

        while (mask_needle != 0) {
            u32 bitpos = count_trailing_zeroes(mask_needle);
            *zero_count += count_bits_set(mask_zero & ~(0xFFFFFFFF << bitpos));

            return s + i + bitpos;
        }

        *zero_count += count_bits_set(mask_zero);
    }

    return null;
}

static char *simd_memchr_count_zeroes_nocase(char *s, usize n, char c, usize *zero_count) {
    *zero_count = 0;

    __m256i lower = _mm256_set1_epi8(tolower(c));
    __m256i upper = _mm256_set1_epi8(toupper(c));
    __m256i zero = _mm256_set1_epi8('\0');

    for (usize i = 0; i < n; i += 32) {
        __m256i block = _mm256_loadu_si256((__m256i *)(s + i));

        __m256i eq_lower = _mm256_cmpeq_epi8(lower, block);
        __m256i eq_upper = _mm256_cmpeq_epi8(upper, block);
        __m256i eq       = _mm256_or_si256(eq_lower, eq_upper);
        __m256i eq_zero  = _mm256_cmpeq_epi8(zero, block);

        u32 mask_needle = _mm256_movemask_epi8(eq);
        u32 mask_zero = _mm256_movemask_epi8(eq_zero);

        while (mask_needle != 0) {
            u32 bitpos = count_trailing_zeroes(mask_needle);
            *zero_count += count_bits_set(mask_zero & ~(0xFFFFFFFF << bitpos));

            return s + i + bitpos;
        }

        *zero_count += count_bits_set(mask_zero);
    }

    return null;
}

static char *simd_memchr(char *s, usize n, char c) {
    __m256i first = _mm256_set1_epi8(c);

    for (usize i = 0; i < n; i += 32) {
        __m256i block = _mm256_loadu_si256((__m256i *)(s + i));
        __m256i eq = _mm256_cmpeq_epi8(first, block);

        u32 mask_needle = _mm256_movemask_epi8(eq);

        while (mask_needle != 0) {
            u32 bitpos = count_trailing_zeroes(mask_needle);
            return s + i + bitpos;
        }

    }

    return null;
}

////////////////////////////////////////////////////////////////
// rune: File IO

static void file_open(file *file, char *path, file_access access) {
    u32 dw_access = 0;
    u32 dw_create = 0;

    switch (access) {
        case FILE_ACCESS_READ: {
            dw_access = GENERIC_READ;
            dw_create = OPEN_EXISTING;
        } break;

        case FILE_ACCESS_WRITE: {
            dw_access = GENERIC_WRITE;
            dw_create = CREATE_ALWAYS;
        } break;

        default: {
            assert(false);
        } break;
    }

    file->handle = CreateFileA(path, dw_access, 0, null, dw_create, FILE_ATTRIBUTE_NORMAL, null);

    if (file->handle != INVALID_HANDLE_VALUE) {
        file->ok = true;
    } else {
        file->ok = false;
        debug_log_error_win32("CreateFileA");
    }
}

static void file_close(file *file) {
    CloseHandle(file->handle);
}

static void file_read_and_allocate(file *file, void **buffer, usize size) {
    if (file->ok) {
        *buffer = heap_alloc(size, false);

        if (*buffer) {
            file_read(file, *buffer, size);
            if (!file->ok) {
                heap_free(*buffer);
                *buffer = null;
            }
        } else {
            file->ok = false;
            assert(false);
        }
    }
}

static void file_read(file *file, void *buffer, usize size) {
    if (file->ok) {
        u32 bytes_read;
        if (ReadFile(file->handle, buffer, (u32)size, &bytes_read, null)) {
            if (bytes_read == size) {
                // All good
            } else {
                file->ok = false;
                debug_log_error("ReadFile read %i bytes but wanted to read %i", bytes_read, (u32)size);
                assert(false);
            }
        } else {
            file->ok = false;
            debug_log_error_win32("ReadFile");
            assert(false);
        }
    }
}

static void file_write(file *file, void *buffer, usize size) {
    if (file->ok) {
        u32 bytes_written;
        if (WriteFile(file->handle, buffer, (u32)size, &bytes_written, null)) {
            if (bytes_written == size) {
                // All good
            } else {
                file->ok = false;
                debug_log_error("WriteFile wrote %i bytes but wanted to write %i", bytes_written, (u32)size);
            }
        } else {
            file->ok = false;
            debug_log_error_win32("WriteFile");
        }
    }
}

static void file_read_u8(file *file, u8 *buffer) { file_read(file, buffer, sizeof(*buffer)); }
static void file_read_u16(file *file, u16 *buffer) { file_read(file, buffer, sizeof(*buffer)); }
static void file_read_u32(file *file, u32 *buffer) { file_read(file, buffer, sizeof(*buffer)); }
static void file_read_u64(file *file, u64 *buffer) { file_read(file, buffer, sizeof(*buffer)); }
static void file_read_usize(file *file, usize *buffer) { file_read(file, buffer, sizeof(buffer)); }

static void file_write_u8(file *file, u8 buffer) { file_write(file, &buffer, sizeof(buffer)); }
static void file_write_u16(file *file, u16 buffer) { file_write(file, &buffer, sizeof(buffer)); }
static void file_write_u32(file *file, u32 buffer) { file_write(file, &buffer, sizeof(buffer)); }
static void file_write_u64(file *file, u64 buffer) { file_write(file, &buffer, sizeof(buffer)); }
static void file_write_usize(file *file, usize buffer) { file_write(file, &buffer, sizeof(buffer)); }

static void file_read_array(file *file, array *array) {
    file_read(file, array, sizeof(*array));
    file_read_and_allocate(file, &array->elems, array->count_allocated * array->elem_size);
}

static void file_write_array(file *file, array array) {
    file_write(file, &array, sizeof(array));
    file_write(file, array.elems, array.count_allocated * array.elem_size);
}

////////////////////////////////////////////////////////////////
// rune: Database


static void db_create(db *db) {
    db->latest_journal_id = 0;
    db->latest_usn = 0;
    db->records_not_in_use_count = 0;

    array_create_size(&db->name_buffer, KILOBYTES(64), true);
    array_create_size(&db->lookup_array, KILOBYTES(64), true);
    array_create_size(&db->record_array, KILOBYTES(64), true);
}

static void db_destroy(db *db) {
    array_destroy(&db->name_buffer);
    array_destroy(&db->record_array);
    array_destroy(&db->lookup_array);
}

static bool db_write_to_file(db *db, char *file_path) {
    // TODO(rune): This should be a transaction, to avoid partial writes.

    file file;
    file_open(&file, file_path, FILE_ACCESS_WRITE);
    file_write_u64(&file, db->latest_journal_id);
    file_write_u64(&file, db->latest_usn);
    file_write_u32(&file, db->records_not_in_use_count);
    file_write_array(&file, db->name_buffer.as_void);
    file_write_array(&file, db->record_array.as_void);
    file_write_array(&file, db->lookup_array.as_void);
    file_close(&file);

    if (file.ok) {
        return true;
    } else {
        assert(false);
        return false;
    }
}

static bool db_create_from_file(db *db, char *file_path) {
    file file;
    file_open(&file, file_path, FILE_ACCESS_READ);
    file_read_u64(&file, &db->latest_journal_id);
    file_read_u64(&file, &db->latest_usn);
    file_read_u32(&file, &db->records_not_in_use_count);
    file_read_array(&file, &db->name_buffer.as_void);
    file_read_array(&file, &db->record_array.as_void);
    file_read_array(&file, &db->lookup_array.as_void);
    file_close(&file);

    if (file.ok) {
        return true;
    } else {
        db_destroy(db);
        return false;
    }
}

static char *db_get_record_name(db *db, record *record) {
    assert(record->name_offset < db->name_buffer.count);
    return &db->name_buffer.elems[record->name_offset];
}

static record *db_get_record_parent(db *db, record *record) {
    return db_get_record_by_id(db, record->parent_id);
}

static record *db_get_record_by_id(db *db, record_id id) {
    usize lookup_count = db->lookup_array.count_allocated;

    // NOTE(rune): The lookup always expands to fit the max record_id_t added, so if we're
    // asking for fild_id_t that doesn't fit in the lookup, the new_record was never added
    // to the database.
    if (id.record_number >= lookup_count) {
        assert(false);
        return null;
    }

    // NOTE(rune): Assert that the lookup does not point outside the new_record buffer
    u64 index_from_lookup = db->lookup_array.elems[id.record_number];
    if (index_from_lookup >= db->record_array.count) {
        assert(false);
        return null;
    }

    if (index_from_lookup == 0) {
        return null;
    }

    record *record_from_lookup = (record *)db->record_array.elems + index_from_lookup;
    if (record_from_lookup) {
        assert(record_from_lookup->id.record_number == id.record_number);
    }

    return record_from_lookup;
}

static void db_mark_record_not_in_use(db *db, record *record) {
    if (!(record->attributes & FILE_ATTRIBUTE_NOT_IN_USE)) {
        record->attributes |= FILE_ATTRIBUTE_NOT_IN_USE;
        db->records_not_in_use_count++;
    }
}

static bool db_refresh_lookup(db *db, record *new_record) {
    if (!array_reserve(&db->lookup_array, new_record->id.record_number + 1, true)) {
        assert(false);
        return false;
    }

    // NOTE(rune): Record indices are not pushed in order onto the database->lookup array, like
    // records are pushed onto the database->records array, so we always mark the whole lookup
    // lookup buffer as used.
    db->lookup_array.count = db->lookup_array.count_allocated;

    record *records = db->record_array.elems;
    u32      *lookup  = db->lookup_array.elems;

    // NOTE(rune): If the lookup previously pointed to another new_record, we mark the previous
    // record as no longer in use, so that it can be pruned later.
    u64 prev_record_index = lookup[new_record->id.record_number];
    if (prev_record_index != 0) {
        assert(prev_record_index < db->record_array.count);
        record *prev_record = &records[prev_record_index];

        if (new_record != prev_record) {
            db_mark_record_not_in_use(db, prev_record);
        }
    }

    u32 new_record_index = (u32)(new_record - records);
    lookup[new_record->id.record_number] = new_record_index;

    return true;
}


static uint32_t length_of_utf16_as_utf8(wchar *wstring, uint32_t wstring_len) {
    uint32_t utf8_length = WideCharToMultiByte(CP_UTF8, 0, wstring, wstring_len, null, 0, null, null);
    return utf8_length;
}

static bool convert_utf16_to_utf8(wchar *wstring, uint32_t wstring_len, char *utf8_buffer, uint32_t utf8_buffer_size) {
    bool result = false;

    result = WideCharToMultiByte(CP_UTF8, 0, wstring, wstring_len, utf8_buffer, utf8_buffer_size, null, null) > 0;
    return result;
}

static record *db_insert(db *db, record_id id, record_id parent_id, uint32_t attributes, wchar *wname, uint32_t wname_len) {
    uint32_t name_len  = length_of_utf16_as_utf8(wname, wname_len);

    char *name = array_push_count(&db->name_buffer, name_len + 1, false);
    if (!name) {
        assert(false);
        return null;
    }

    bool converted = convert_utf16_to_utf8(wname, wname_len, name, name_len);
    if (!converted) {
        assert(false);
        return null;
    }

    name[name_len] = '\0';

    record *record = array_push(&db->record_array, false);
    if (!record) {
        assert(false);
        return null;
    }

    record->id          = id;
    record->parent_id   = parent_id;
    record->attributes  = attributes;
    record->name_offset = name - db->name_buffer.elems;

    db_refresh_lookup(db, record);

    return record;
}


static record *db_update(db *db, record_id id, record_id parent_id, u32 attributes, wchar *wname, u32 wname_len) {
    db_delete(db, id);

    return db_insert(db, id, parent_id, attributes, wname, wname_len);
}

static void db_delete(db *db, record_id id) {
    record *record = db_get_record_by_id(db, id);
    if (record) {
        db_mark_record_not_in_use(db, record);
    }
}

static void db_apply_changes(db *db, change_list changes) {
#if 1
    printf("================================\n");
    for (change *change = changes.first; change; change = change->next) {
        if (change->ignore) {
            printf(ANSI_BG_DARK_GRAY);
        }

        printf("%16llx", change->usn);

        switch (change->type) {
            case CHANGE_TYPE_INSERT: printf(ANSI_FG_GREEN "INSERT " ANSI_RESET); break;
            case CHANGE_TYPE_UPDATE: printf(ANSI_FG_CYAN  "UPDATE " ANSI_RESET); break;
            case CHANGE_TYPE_DELETE: printf(ANSI_FG_RED   "DELETE " ANSI_RESET); break;
        }

        printf("%16llx %16llx %.*ls\n",
               change->id.id64,
               change->parent_id.id64,
               change->wname_length,
               change->wname);
    }
#endif

    for (change *change = changes.first; change; change = change->next) {
        if (!change->ignore) {
            switch (change->type) {
                case CHANGE_TYPE_INSERT: {
                    db_insert(db,
                              change->id,
                              change->parent_id,
                              change->attributes,
                              change->wname,
                              change->wname_length);
                } break;

                case CHANGE_TYPE_UPDATE: {
                    db_update(db,
                              change->id,
                              change->parent_id,
                              change->attributes,
                              change->wname,
                              change->wname_length);
                } break;

                case CHANGE_TYPE_DELETE: {
                    db_delete(db, change->id);
                } break;
            }
        }

        db->latest_usn = max(db->latest_usn, change->usn);
    }
}

static uint32_t db_prune(db *db) {
    // TODO(rune): Implement
}

////////////////////////////////////////////////////////////////
// rune: Sanity checks

// NOTE(rune): Counts the number of names in the database's name buffer, and compares
// it to the number of records in the database. When the run_query static searches
// the name buffer, it uses the number of null chars encountered the determine the
// matched record's index. Therefore its important that the number of null chars in
// the name buffer always matches the number of records in the record buffer.
static bool debug_sanity_check_names(db *db) {
    usize n = 0;

    for (usize i = 0; i < db->name_buffer.count; i++) {
        char c = db->name_buffer.elems[i];
        if (c == '\0') {
            n++;
        }
    }

    u64 record_count = db->record_array.count;
    if (n == record_count) {
        return true;
    } else {
        assert(!"Number of null-chars in name_buffer does not match number of records.");
        return false;
    }
}

static bool debug_sanity_check_lookup(db *db) {
    record *records      = db->record_array.elems;
    usize   record_count = db->record_array.count;

    u32  *lookup        = db->lookup_array.elems;
    usize lookup_count  = db->lookup_array.count;

    // NOTE(rune): Check lookup_array -> record_array.
    for (usize record_number = 0; record_number < lookup_count; record_number++) {
        u32 idx_from_lookup = lookup[record_number];

        if (idx_from_lookup == 0) {
            continue;
        }

        if (idx_from_lookup > record_count) {
            assert(!"Lookup points to record outside record buffer.");
            return false;
        }

        record *record = records + idx_from_lookup;

        if (record->id.record_number != record_number) {
            assert(!"Lookup does not match record_number of record.");
            return false;
        }
    }

    // NOTE(rune): Check record_array -> lookup_array.
    for (usize record_idx = 0; record_idx < record_count; record_idx++) {
        record *it = records + record_idx;

        if (it->id.record_number >= lookup_count) {
            assert(!"Lookup does not contain all record numbers.");
            return false;
        }

        u32 idx_from_lookup = lookup[it->id.record_number];
        record *record_from_lookup = records + idx_from_lookup;

        if (idx_from_lookup == 0 && it->id.record_number != 0) {
            assert(!"Lookup does not contain all record numbers.");
            return false;
        }

        if (it->id.record_number != it->id.record_number) {
            assert(!"Lookup does not match record_number of record.");
            return false;
        }

        u32 parent_idx_from_lookup = lookup[it->parent_id.record_number];
        if (parent_idx_from_lookup == 0 && it->parent_id.record_number != 0) {
            assert(!"Lookup does not contain parent record.");
            return false;
        }
    }

    return true;
}

////////////////////////////////////////////////////////////////
// rune: Query

static bool walk_ancestors_is_child_of_root(record *record, db *database, u32 max_depth) {
    while (max_depth--) {
        bool is_root = record->id.id64 == record->parent_id.id64;
        if (is_root) {
            return true;
        }

        record = db_get_record_parent(database, record);
        if (!record) {
            // TODO(rune): Mark as orphan?
            return false;
        }
    }

    assert(true);
    return false;
}

static u32 walk_ancestors_build_path(record *child, db *database,
                                     record **ancestor_buffer, u32 ancestor_buffer_count,
                                     char *path_buffer, usize path_buffer_size) {
    // NOTE(rune): The child record itself also counts as an ancestor
    u32 ancestor_count = 1;

    while (true) {
        ancestor_buffer[ancestor_count - 1] = child;

        bool is_root = child->id.id64 == child->parent_id.id64;
        if (is_root) {
            break;
        }

        child = db_get_record_parent(database, child);
        if (!child) {
            // TODO(rune): Mark as orphan?
            return 0;
        }

        ancestor_count++;

        if (ancestor_count > ancestor_buffer_count) {
            assert(false);
            return 0;
        }
    }

    path_buffer[0] = 'C';
    path_buffer[1] = ':';
    path_buffer[2] = '\0';

    // TODO(rune): String concatenation optimization. StringCbCatA presumably checks length of pszDest
    // each time it is called, which is not neccassary, when we are concatenating multiple strings.

    // NOTE(rune): Start at ancestor_count - 2 to skip root record since its name just "."
    for (i32 ancestor_index = ancestor_count - 2;
         ancestor_index >= 0;
         ancestor_index--) {
        char *name = db_get_record_name(database, ancestor_buffer[ancestor_index]);

        if (StringCbCatA(path_buffer, path_buffer_size, "\\")) {
            assert(false);
            return 0;
        }

        if (StringCbCatA(path_buffer, path_buffer_size, name)) {
            assert(false);
            return 0;
        }
    }

    return ancestor_count;
}

static bool matches_query_flags(record *record, quickfind_flags flags,
                                char *query, usize query_len,
                                char *match, usize match_len) {
    if (record->attributes & FILE_ATTRIBUTE_NOT_IN_USE) {
        return false;
    }

    if ((flags & QUICKFIND_FLAG_ONLY_FILES) && (record->attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return false;
    }

    if ((flags & QUICKFIND_FLAG_ONLY_DIRECTORIES) && !(record->attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return false;
    }

    if ((flags & QUICKFIND_FLAG_FULLNAME) && (query_len != match_len)) {
        return false;
    }

    return true;
}

static char *find_first_occurrence_and_count_nulls(char *buffer, usize buffer_length, char *look_for, usize look_for_length, quickfind_flags flags, usize *null_count) {
    if (flags & QUICKFIND_FLAG_CASE_SENSITIVE) {
        if (look_for_length == 1) {
            return simd_memchr_count_zeroes(buffer, buffer_length, *look_for, null_count);
        } else {
            return simd_memmem_count_zeroes(buffer, buffer_length, look_for, look_for_length, null_count);
        }
    } else {
        if (look_for_length == 1) {
            return simd_memchr_count_zeroes_nocase(buffer, buffer_length, *look_for, null_count);
        } else {
            return simd_memmem_count_zeroes_nocase(buffer, buffer_length, look_for, look_for_length, null_count);
        }
    }
}

// NOTE(rune): query_result_item_t's are pushed to result_buffer.
static query_result run_query(quickfind_params params, buffer *result_buffer, db *database) {
    usize search_buffer_size       = database->name_buffer.count;
    char *search_buffer_base       = database->name_buffer.elems;
    char *current_search           = search_buffer_base;

    u64 found_count             = 0;
    u32 return_count            = 0;
    usize current_record_index  = 0;

    while (found_count < params.stop_count) {
        usize null_count = 0;
        char *match = find_first_occurrence_and_count_nulls(current_search,
                                                            (search_buffer_base + search_buffer_size) - current_search,
                                                            params.text,
                                                            params.text_length,
                                                            params.flags,
                                                            &null_count);

        if (match == null) {
            break;
        }

        if (match > database->name_buffer.elems + database->name_buffer.count) {
            break;
        }

        current_search = simd_memchr(match, (search_buffer_base + search_buffer_size) - match, '\0');
        if (current_search == null) {
            assert(false);
            break;
        }

        // NOTE(rune): Names in the name buffer are always stored in the exact same order
        // as records in the record array, so we can just use the number of names searched
        // as an index into the record array.
        current_record_index += null_count;
        if (current_record_index >= database->record_array.count) {
            assert(false);
            break;
        }

        record *found     = &database->record_array.elems[current_record_index];
        char *name        = db_get_record_name(database, found);
        usize name_length = current_search - name;

        char path_buffer[256 * 256];
        record *ancestor_buffer[256];

        if (found && !(found->attributes & FILE_ATTRIBUTE_NOT_IN_USE)) {
            if (matches_query_flags(found, params.flags, params.text, params.text_length, name, name_length)) {
                if (walk_ancestors_is_child_of_root(found, database, 256)) {
                    if ((found_count >= params.skip_count) && (return_count < params.return_count)) {
                        if (walk_ancestors_build_path(found, database, ancestor_buffer, countof(ancestor_buffer), path_buffer, sizeof(path_buffer))) {
                            u32 path_size             = (u32)(strnlen(path_buffer, sizeof(path_buffer)) + 1);
                            u32 result_size           = sizeof(query_result_item) + path_size;
                            query_result_item *result = buffer_append(result_buffer, result_size);
                            if (!result) {
                                query_result result = { QUICKFIND_ERROR_OUT_OF_MEMORY };
                                return result;
                            }

                            result->id          = found->id.id64;
                            result->attributes  = found->attributes;
                            result->path_size   = path_size;
                            memcpy(&result->path, path_buffer, path_size);

                            return_count++;
                        }
                    }

                    found_count++;
                }
            }
        }
    }

    query_result result = { QUICKFIND_OK, found_count, return_count };
    return result;
}

////////////////////////////////////////////////////////////////
// rune: Server

static void server_acquire_write_lock(server *server) {
    AcquireSRWLockExclusive(&server->database_lock);
}

static void server_release_write_lock(server *server) {
    ReleaseSRWLockExclusive(&server->database_lock);
}

static void server_acquire_read_lock(server *server) {
    AcquireSRWLockExclusive(&server->database_lock);
}

static void server_release_read_lock(server *server) {
    ReleaseSRWLockExclusive(&server->database_lock);
}

static bool server_get_database_file_path(char *buffer, usize buffer_size) {
    char common_appdata_path[MAX_PATH];
    if (SHGetFolderPathA(null, CSIDL_COMMON_APPDATA, null, 0, common_appdata_path)) {
        debug_log_error_win32("SHGetFolderPathA");
        return false;
    }

    buffer[0] = '\0';
    if (StringCchCatA(buffer, buffer_size, common_appdata_path)) {
        debug_log_error_win32("StringCchCatA");
        return false;
    }

    if (StringCchCatA(buffer, buffer_size, "\\quickfind.db")) {
        debug_log_error_win32("StringCchCatA");
        return false;
    }

    return true;
}

static DWORD WINAPI server_worker_thread_proc(LPVOID lpParameter) {
    server *server = lpParameter;

    server_acquire_write_lock(server);

    bool could_load_database_file = db_create_from_file(&server->database, server->database_path);
    if (could_load_database_file) {
        server->database_initialized = true;
    } else {
        // NOTE(rune): Could not load database from file (either it is the first time
        // the quickfind service is launched, or something is wrong with the file), so
        // we reconstruct the database by iterating over the master file table.

        db_create(&server->database);

        ntfs_usn_journal_data journal_data;
        if (ntfs_get_journal_data(&journal_data, 'C')) {
            server->database.latest_journal_id = journal_data.journal_id;
            server->database.latest_usn        = journal_data.next_usn;

            u32 buffer_size = MEGABYTES(1);
            void *buffer = heap_alloc(buffer_size, false);

            ntfs_mft_iter iterator;
            if (ntfs_mft_iter_open(&iterator, 'C', buffer, buffer_size) == NTFS_ERROR_NONE) {
                ntfs_parsed_mft_record parsed_record;
                while (ntfs_mft_iter_advance(&iterator, &parsed_record)) {
                    if (parsed_record.parse_error == NTFS_ERROR_NONE) {
                        db_insert(&server->database, parsed_record.id, parsed_record.parent_id,
                                  parsed_record.attributes, parsed_record.name, parsed_record.name_len);
                    }
                }

                ntfs_mft_iter_close(&iterator);

                server->database_initialized = true;
            }
        }
    }

    server_release_write_lock(server);

    u32 i = 0;
    while (!server->shutdown) {
        i++;

        if (server->database_initialized) {
#if 1
            buffer buffer = {
                .data     = server->usn_query_storage,
                .capacity = sizeof(server->usn_query_storage)
            };

            change_list changes = ntfs_get_usn_journal_changes(&buffer, &server->database);
            server_acquire_write_lock(server);
            db_apply_changes(&server->database, changes);
            server_release_write_lock(server);

            debug_sanity_check_names(&server->database);
            debug_sanity_check_lookup(&server->database);
#endif

            // Write database to disk every minute
            if (i % 60 == 0) {
                server_acquire_read_lock(server);
                db_write_to_file(&server->database, server->database_path);
                server_release_read_lock(server);
            }

#if 1
            debug_sanity_check_names(&server->database);
            debug_sanity_check_lookup(&server->database);
#endif
        }

        // Look for changes in usn journal every second
        WaitForSingleObject(server->shutdown_event, 1000);

        print_tracked_allocations(true, false);
    }

    return 0;
}

static HANDLE server_create_event(BOOL manual_reset) {
    HANDLE event = CreateEventA(null, manual_reset, false, null);
    if (event == null) {
        assert(false);
        debug_log_error_win32("CreateEventA");
        return null;
    }

    return event;
}

static HANDLE server_create_thread(LPTHREAD_START_ROUTINE start, void *param) {
    HANDLE thread = CreateThread(0, 0, start, param, 0, 0);
    if (thread == null) {
        assert(false);
        debug_log_error_win32("CreateThread");
        return null;
    }

    return thread;
}

static HANDLE server_create_pipe() {
    HANDLE pipe = INVALID_HANDLE_VALUE;

    SECURITY_ATTRIBUTES security_attributes  = { 0 };
    security_attributes.nLength              = sizeof(security_attributes);

    SECURITY_DESCRIPTOR security_descriptor  = { 0 };
    security_attributes.lpSecurityDescriptor = &security_descriptor;

    u8 buffer[8192] = { 0 };

    SID *win_world_sid = (SID *)(buffer + 0);
    ACL *acl           = (ACL *)(buffer + 4096);

    u32 win_world_sid_size = 4096;

    u32 access_mask =
        SYNCHRONIZE |
        READ_CONTROL |
        FILE_LIST_DIRECTORY |
        FILE_READ_ATTRIBUTES |
        FILE_READ_DATA |
        FILE_READ_EA |
        FILE_WRITE_DATA |
        FILE_WRITE_ATTRIBUTES |
        FILE_WRITE_EA;

    //
    // NOTE(rune): Give WinWorldSid (everyone) read/write access to the pipe.
    //

    bool error = false;
    if (!error) {
        if (!InitializeSecurityDescriptor(&security_descriptor, SECURITY_DESCRIPTOR_REVISION)) {
            debug_log_error_win32("InitializeSecurityDescriptor");
            error = true;
        }
    }

    if (!error) {
        if (!InitializeAcl(acl, 4096, ACL_REVISION)) {
            debug_log_error_win32("InitializeAcl");
            error = true;
        }
    }

    if (!error) {
        if (!CreateWellKnownSid(WinWorldSid, null, win_world_sid, &win_world_sid_size)) {
            debug_log_error_win32("CreateWellKnownSid");
            error = true;
        }
    }

    if (!error) {
        if (!AddAccessAllowedAce(acl, ACL_REVISION, access_mask, win_world_sid)) {
            debug_log_error_win32("AddAccessAllowedAce");
            error = true;
        }
    }

    if (!error) {
        if (!SetSecurityDescriptorDacl(&security_descriptor, true, acl, false)) {
            debug_log_error_win32("SetSecurityDescriptorDacl");
            error = true;
        }
    }

    if (!error) {
        pipe = CreateNamedPipeA(QUICKFIND_PIPE_NAME,
                                PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED,
                                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                1, // 1 max instance
                                MEGABYTES(4),
                                MEGABYTES(4),
                                0, // default wait time
                                &security_attributes);

        if (pipe == INVALID_HANDLE_VALUE) {
            debug_log_error_win32("CreateNamedPipeA");
            error = true;
        }
    }

    if (error) {
        CloseHandle(pipe);
    }

    return pipe;
}

static void server_calculate_response(server *server) {
    msg *req = &server->request;
    msg *res = &server->response;

    memset(&res->head, 0, sizeof(res->head));

    switch (req->head.type) {
        case MSG_TYPE_QUERY_REQUEST: {
            if (server->database_initialized) {
                quickfind_params params = { 0 };
                params.text         = req->body;
                params.text_length  = req->head.body_size;
                params.flags        = req->head.query_request.flags;
                params.return_count = req->head.query_request.return_count;
                params.skip_count   = req->head.query_request.skip_count;
                params.stop_count   = req->head.query_request.stop_count;

                server_acquire_read_lock(server);
                buffer result_buffer = {
                    .data = res->body,
                    .capacity = sizeof(res->body),
                };
                query_result query_result = run_query(params, &result_buffer, &server->database);
                server_release_read_lock(server);

                if (!query_result.error) {
                    res->head.type                        = MSG_TYPE_QUERY_RESPONSE;
                    res->head.query_response.found_count  = query_result.found_count;
                    res->head.query_response.return_count = query_result.return_count;
                    res->head.body_size                   = (u32)result_buffer.size;
                } else {
                    res->head.error = query_result.error;
                }
            } else {
                res->head.error = QUICKFIND_ERROR_DATABASE_NOT_INITIALIZED;
            }
        } break;

        default: {
            res->head.body_size = QUICKFIND_ERROR_INVALID_REQUEST;
        } break;
    }
}

static bool server_create(server *server) {
    zero_struct(server);

    InitializeSRWLock(&server->database_lock);
    server_get_database_file_path(server->database_path, sizeof(server->database_path));

    server->connection_event = server_create_event(false);
    server->shutdown_event   = server_create_event(true);
    server->pipe             = server_create_pipe();
    server->worker_thread    = server_create_thread(server_worker_thread_proc, server);

    return true;
}

static void server_destroy(server *server) {
    CloseHandle(server->worker_thread);
    CloseHandle(server->pipe);
    CloseHandle(server->connection_event);
    CloseHandle(server->shutdown_event);
}

static bool server_run(server *server) {
    while (!server->shutdown) {
        OVERLAPPED overlapped = { 0 };
        overlapped.hEvent = server->connection_event;

        if (!ConnectNamedPipe(server->pipe, &overlapped)) {
            switch (GetLastError()) {
                case ERROR_IO_PENDING: {
                    HANDLE wait_objects[2] = { server->connection_event, server->shutdown_event };
                    u32 wait_object_idx = WaitForMultipleObjects(countof(wait_objects), wait_objects, false, INFINITE);

                    if (server->shutdown) {
                        break;
                    }
                } // fallthrough;

                case ERROR_PIPE_CONNECTED: {
                    quickfind_error error = pipe_read_msg(server->pipe, &server->request);
                    if (!error) {
                        server_calculate_response(server);
                        pipe_write_msg(server->pipe, &server->response);
                    }

                    FlushFileBuffers(server->pipe);
                    DisconnectNamedPipe(server->pipe);
                } break;

                default: {
                    assert(false);
                    debug_log_error_win32("ConnectNamedPipe");
                } break;
            }
        }
    }


    // Save database to disk before shutting down
    server_acquire_read_lock(server);
    db_write_to_file(&server->database, server->database_path);
    server_release_read_lock(server);

    return QUICKFIND_OK;
}

static void server_begin_shutdown(server *server) {
    server->shutdown = true;
    SetEvent(server->shutdown_event);
}
