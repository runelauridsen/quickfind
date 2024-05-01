
static void ntfs_fixup_record(ntfs_mft_record_header *f) {
    unsigned char *record = (unsigned char *)f;

    // there will be three items in the fixup array*
    // first item = the check value */
    // second value = to replace the last word in the first cluster, if check values match */
    // third value = to replace the last word in the second cluster, if check values match */
    unsigned short size = f->update_sequence_size;
    unsigned short *offset = (WORD *)(record + f->update_sequence_offset);

#define SEQUENCE_NUMBER_STRIDE          512

    unsigned int j = 1;
    for (unsigned int i = SEQUENCE_NUMBER_STRIDE; i <= NTFS_FILE_RECORD_SIZE; i += SEQUENCE_NUMBER_STRIDE) {
        unsigned short *value = (WORD *)(record + i - 2);

        if (*value == offset[0]) {
            *value = offset[j];
        } else if (offset[j] != *value) {
            return;
        }

        j++;
    }
}

static bool ntfs_read_from_volume(HANDLE volume, void *buffer, u32 size, u64 from) {
    DWORD bytes_read;
    LONG high = from >> 32;

    // NOTE(rune): Error checking for SetFilePointer is stupid.
    // https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfilepointer

    DWORD ptr_low = SetFilePointer(volume, from & 0xFFFFFFFF, &high, FILE_BEGIN);
    if ((ptr_low != INVALID_SET_FILE_POINTER) || (GetLastError() == NO_ERROR)) {
        if (ReadFile(volume, buffer, size, &bytes_read, null)) {
            if (bytes_read == size) {
                return true;
            } else {
                assert(false);
                return false;
            }
        } else {
            printf("ReadFile failed: %i\n", GetLastError());
            assert(false);
            return false;
        }
    } else {
        printf("SetFilePointer failed: %i\n", GetLastError());
        assert(false);
        return false;
    }
}

// NOTE(rune): Parses the length of offset fields of a data run entry and returns a pointer the next data run entry.
static u8 *ntfs_next_datarun(u8 *datarun_start, u64 *length_in_clusters, u64 *offset_in_clusters) {
    *length_in_clusters = 0;
    *offset_in_clusters = 0;

    ntfs_datarun_header *header = (ntfs_datarun_header *)datarun_start;

    // NOTE(rune): Length and offset are stored as variable-sized integers, see:
    // http://inform.pucp.edu.pe/~inf232/Ntfs/ntfs_doc_v0.5/concepts/data_runs.html
    u32 i = 0;

    for (u32 j = 0; j < header->length_field_size; j++) {
        *length_in_clusters += ((u64)header->data[i]) << (j * 8);
        i++;
    }

    for (u32 j = 0; j < header->offset_field_size; j++) {
        *offset_in_clusters += ((u64)header->data[i]) << (j * 8);
        i++;
    }

    u64 sign_mask = 0x80llu << (header->offset_field_size * 8 - 8);
    if (*offset_in_clusters & sign_mask) {
        for (u32 j = header->offset_field_size; j < sizeof(u64); j++) {
            *offset_in_clusters |= (0xFFllu << (j * 8));
        }
    }

    return datarun_start + i + 1;
}

// NOTE(rune): Moves attribute to point the to next attribute.
// Returns false if there are no more attributes.
static bool ntfs_next_attribute(ntfs_mft_record *record, ntfs_attribute **attribute) {
    ntfs_attribute *current = *attribute;

    if (current == null) {
        *attribute = (ntfs_attribute *)(record->bytes + record->header.first_attribute_offset);
        return true;
    } else {
        u8 *current_ptr = (u8 *)(current);
        u8 *next_ptr = current_ptr + (current)->attribute_size;
        ntfs_attribute *next = (ntfs_attribute *)(next_ptr);

        if (next_ptr < (record->bytes + sizeof(record->bytes))) {
            if (next->attribute_type != NTFS_ATTRIBUTE_TYPE_END) {
                *attribute = next;
                return true;
            } else {
                return false;
            }
        } else {
            // NOTE: Next attribute is outside the record, something is corrupt.
            return false;
        }
    }
}

static u64 ntfs_get_absolute_offset_of_record_number(u64 find_number, ntfs_parsed_datarun *dataruns, u32 datarun_count) {
    u64 record_sum = 0;

    for (u32 i = 0; i < datarun_count; i++) {
        u64 records_in_run = dataruns[i].length / NTFS_FILE_RECORD_SIZE;

        u64 run_start_number = record_sum;
        u64 run_end_number   = record_sum + records_in_run;

        if ((run_start_number <= find_number) && (run_end_number > find_number)) {
            u64 number_offset   = find_number - run_start_number;
            u64 absolute_offset = dataruns[i].offset + (number_offset * NTFS_FILE_RECORD_SIZE);
            return absolute_offset;
        }

        record_sum += records_in_run;
    }

    return 0;
}

// TODO(rune): Handle hard links
static void ntfs_parse_mft_record(ntfs_mft_iter *iterator, ntfs_mft_record *record, ntfs_parsed_mft_record *parsed_record) {
    ntfs_mft_record_header *header = &record->header;

    if (header->magic_number == NTFS_MAGIC_NUMBER) {
        if (header->is_in_use) {
            ntfs_fixup_record(header);

            // Loop through the records attributes, until we find a $FILE_NAME attribute, that is not in the DOS namespace.
            ntfs_attribute *attribute = null;
            ntfs_file_name_attribute *name_attribute = null;
            while (ntfs_next_attribute(record, &attribute)) {
                switch (attribute->attribute_type) {
                    case NTFS_ATTRIBUTE_TYPE_FILE_NAME: {
                        if (attribute->is_non_resident == false) {
                            ntfs_file_name_attribute *check_name_attribute = (ntfs_file_name_attribute *)attribute;
                            if (check_name_attribute->namespace != NTFS_NAMESPACE_DOS) {
                                name_attribute = check_name_attribute;
                                goto name_attribute_found;
                            }
                        } else {
                            assert(false);
                            parsed_record->parse_error = NTFS_ERROR_PARSE_FILE_NAME_ATTRIBUTE_NON_RESIDENT;
                            return;
                        }
                    } break;

                    case NTFS_ATTRIBUTE_TYPE_ATTRIBUTE_LIST: {
                        if (attribute->is_non_resident == false) {
                            u8 *attribute_ptr  = (u8 *)attribute;
                            u8 *list_entry_ptr = (attribute_ptr + sizeof(ntfs_resident_attribute));

                            while (list_entry_ptr < (attribute_ptr + attribute->attribute_size)) {
                                ntfs_list_entry *list_entry = (ntfs_list_entry *)(list_entry_ptr);

                                u64 sequence_number_mask = 0xffffll << 48;
                                u64 entry_record_number  = list_entry->record_number & (~sequence_number_mask);

                                // NOTE(rune): If the file name attribute is stored resident this record, we will find
                                // it in case NTFS_ATTRIBUTE_TYPE_FILE_NAME instead
                                if ((entry_record_number != header->record_number) &&
                                    (list_entry->attribute_type == NTFS_ATTRIBUTE_TYPE_FILE_NAME) &&
                                    (list_entry->starting_vcn == 0)) {
                                    u64 offset = ntfs_get_absolute_offset_of_record_number(entry_record_number,
                                                                                           iterator->data_runs,
                                                                                           iterator->data_run_count);

                                    ntfs_mft_record other_record;
                                    if (ntfs_read_from_volume(iterator->volume, &other_record, sizeof(other_record), offset)) {
                                        if (other_record.header.record_number == entry_record_number) {
                                            ntfs_attribute *other_attribute = null;
                                            while (ntfs_next_attribute(&other_record, &other_attribute)) {
                                                if ((other_attribute->attribute_type == NTFS_ATTRIBUTE_TYPE_FILE_NAME) &&
                                                    (other_attribute->is_non_resident == false)) {
                                                    ntfs_file_name_attribute *other_name_attribute = (ntfs_file_name_attribute *)other_attribute;
                                                    if (other_name_attribute->namespace != NTFS_NAMESPACE_DOS) {
                                                        name_attribute = other_name_attribute;
                                                        goto name_attribute_found;
                                                    }
                                                }
                                            }
                                        } else {
                                            assert(false);
                                        }
                                    }
                                }

                                list_entry_ptr += list_entry->entry_length;
                            }
                        }
                    } break;
                }
            }

        name_attribute_found:
            if (name_attribute != null) {
                record_id id        = { header->record_number, header->sequence_number };
                record_id parent_id = { name_attribute->parent_record_number, name_attribute->parent_sequence_number };

                // TODO(rune): Regular file attributes
                parsed_record->id            = id;
                parsed_record->parent_id     = parent_id;
                parsed_record->name          = name_attribute->file_name;
                parsed_record->name_len   = name_attribute->file_name_length;
            } else {
                parsed_record->parse_error = NTFS_ERROR_PASRE_FILE_NAME_ATTRIBUTE_MISSING;
            }
        } else {
            parsed_record->parse_error = NTFS_ERROR_PARSE_RECORD_NOT_IN_USE;
        }
    } else {
        parsed_record->parse_error = NTFS_ERROR_PARSE_RECORD_NO_MAGIC_NUMBER;
    }
}

// NOTE(rune): Opens a handle to the drive_letter's volume, and parses the first MFT record,
// which is the record for the MFT itself, and stores the parsed data runs in iterator->data_runs.
// The data runs describe were the MFT data is stored on disk. The iterator should be closed
// with ntfs_file_record_iterator_close, even if this function returns an error.
static ntfs_error ntfs_mft_iter_open(ntfs_mft_iter *iter, char drive_letter, void *buffer, u32 buffer_size) {
    static_assert(sizeof(ntfs_boot_sector) == 512, "Size of boot sector should be 512 bytes");
    static_assert(sizeof(ntfs_mft_record) == 1024, "Size of ntfs file record should nbe 1024 bytes");
    assert(buffer_size % sizeof(ntfs_mft_record) == 0);

    ntfs_error error = NTFS_ERROR_NONE;

    memset(iter, 0, sizeof(ntfs_mft_iter));
    iter->buffer      = buffer;
    iter->buffer_size = buffer_size;

    char volume_path[] = "\\\\.\\X:";
    volume_path[4] = drive_letter;

    iter->volume = CreateFileA(volume_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, null, OPEN_EXISTING, 0, null);
    if (iter->volume != INVALID_HANDLE_VALUE) {
        // Read boot sector to locate master file table
        ntfs_boot_sector boot_sector;
        if (ntfs_read_from_volume(iter->volume, &boot_sector, 512, 0)) {
            iter->bytes_per_sector  = boot_sector.bytes_per_sector;
            iter->bytes_per_cluster = boot_sector.bytes_per_sector * boot_sector.sectors_per_cluster;

            // Read the master file table's own record
            u64 mft_file_offset = boot_sector.mft_start * iter->bytes_per_cluster;
            ntfs_mft_record mft_record;
            if (ntfs_read_from_volume(iter->volume, &mft_record, sizeof(mft_record), mft_file_offset)) {
                // Parse the master file table's data run attribute and store the parsed data runs in iterator.
                ntfs_attribute *attribute = null;
                while (ntfs_next_attribute(&mft_record, &attribute)) {
                    if (attribute->attribute_type == NTFS_ATTRIBUTE_TYPE_DATA) {
                        if (attribute->is_non_resident) {
                            u64 cluster = 0;

                            ntfs_non_resident_attribute *data_attribute = (ntfs_non_resident_attribute *)attribute;
                            u8 *data_run_ptr = ((u8 *)data_attribute) + data_attribute->data_runs_offset;

                            while ((*data_run_ptr) && (iter->data_run_count < countof(iter->data_runs))) {
                                u64 length_in_clusters;
                                u64 offset_in_clusters;

                                data_run_ptr = ntfs_next_datarun(data_run_ptr, &length_in_clusters, &offset_in_clusters);

                                cluster += offset_in_clusters;

                                iter->data_runs[iter->data_run_count].length = length_in_clusters * iter->bytes_per_cluster;
                                iter->data_runs[iter->data_run_count].offset = cluster * iter->bytes_per_cluster;
                                iter->data_run_count++;
                            }
                        } else {
                            assert(false);
                            error = NTFS_ERROR_DATA_ATTRIBUTE_NON_RESIDENT;
                        }
                    }
                }
            } else {
                assert(false);
                error = NTFS_ERROR_COULD_NOT_READ_MFT_RECORD;
            }
        } else {
            assert(false);
            error = NTFS_ERROR_COULD_NOT_READ_BOOT_SECTOR;
        }
    } else {
        printf("CreateFileA failed: %i\n", GetLastError());
        assert(false);
        error = NTFS_ERROR_COULD_NOT_OPEN_VOLUME;
    }

    return error;
}

static bool ntfs_mft_iter_advance(ntfs_mft_iter *iter, ntfs_parsed_mft_record *parsed_record) {
    memset(parsed_record, 0, sizeof(ntfs_parsed_mft_record));

    //
    // Read current data run in chunks of iterator->buffer_size
    //

    if (iter->current_offset_in_datarun % iter->buffer_size == 0) {
        u64 read_from = iter->data_runs[iter->current_datarun].offset + iter->current_offset_in_datarun;

        if (ntfs_read_from_volume(iter->volume, iter->buffer, iter->buffer_size, read_from)) {
        } else {
            parsed_record->parse_error = NTFS_ERROR_COULD_NOT_READ_FROM_VOLUME;
            assert(false);
            return false;
        }
    }

    //
    // Locate next record in buffer and parse
    //

    u64 record_offset_in_buffer = iter->current_offset_in_datarun % iter->buffer_size;
    ntfs_mft_record *record   = (ntfs_mft_record *)(iter->buffer + record_offset_in_buffer);
    ntfs_parse_mft_record(iter, record, parsed_record);

    //
    // Move to next data run
    //

    iter->current_offset_in_datarun += NTFS_FILE_RECORD_SIZE;
    if (iter->data_runs[iter->current_datarun].length <= iter->current_offset_in_datarun) {
        iter->current_datarun++;
        iter->current_offset_in_datarun = 0;

        if (iter->current_datarun == iter->data_run_count) {
            return false;
        }
    }

    return true;
}

static void ntfs_mft_iter_close(ntfs_mft_iter *iterator) {
    CloseHandle(iterator->volume);
}

static void ntfs_usn_mark_ignore(change_list changes) {
    for (change *i = changes.last; i; i = i->prev) {
        for (change *j = changes.first; j; j = j->next) {
            if (i != j && i->id.id64 == j->id.id64) {
                change_type ci = i->type;
                change_type cj = j->type;

                // NOTE(rune): Ignore temp files
                if (ci == CHANGE_TYPE_DELETE && cj == CHANGE_TYPE_INSERT) {
                    i->ignore = true;
                    j->ignore = true;
                }

                // NOTE(rune): Ignore update before delete
                if (ci == CHANGE_TYPE_DELETE && cj == CHANGE_TYPE_UPDATE) {
                    j->ignore = true;
                }

                // NOTE(rune): Ignore delete before delete
                if (ci == CHANGE_TYPE_DELETE && cj == CHANGE_TYPE_DELETE) {
                    j->ignore = true;
                }

                // NOTE(rune): Ignore insert before update
                if (ci == CHANGE_TYPE_UPDATE && cj == CHANGE_TYPE_INSERT) {
                    j->ignore = true;
                }
            }
        }
    }
}

static record_id ntfs_id64_from_id128(FILE_ID_128 id128) {
    record_id id64 = { 0 };
    id64.id64 = *((u64 *)(&id128));
    return id64;
}

static bool ntfs_get_journal_data(ntfs_usn_journal_data *journal_data, char drive_letter) {
    char *volume_path = "\\\\?\\c:";
    HANDLE handle = CreateFileA(volume_path,
                                FILE_GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                null,
                                OPEN_EXISTING,
                                0,
                                null);

    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }


    USN_JOURNAL_DATA_V2 journal_data_buffer = { 0 };
    DWORD bytes_read = 0;

    BOOL device_io_result = DeviceIoControl(handle,
                                            FSCTL_QUERY_USN_JOURNAL,
                                            null,
                                            0,
                                            &journal_data_buffer,
                                            sizeof(journal_data_buffer),
                                            &bytes_read,
                                            null);

    CloseHandle(handle);

    if (device_io_result) {
        journal_data->journal_id = journal_data_buffer.UsnJournalID;
        journal_data->next_usn   = journal_data_buffer.NextUsn;
        return true;
    } else {
        return false;
    }
}

static void change_list_add(change_list *list, change *node) {
    if (list->first == null) {
        assert(list->last == null);
        list->first = node;
        list->last = node;
    } else {
        node->prev = list->last;
        list->last->next = node;
        list->last = node;
    }
}

#define USN_BUFFER_SIZE KILOBYTES(64)

// TODO(rune): Make this static use fixed sized buffer instead of dynbuffer_t.
// Maybe also add an UsnJournalIterator thing?
static change_list ntfs_get_usn_journal_changes(buffer *buffer, db *database) {
    change_list changes = { 0 };

    char *volume_path = "\\\\?\\c:";
    HANDLE handle = CreateFileA(volume_path,
                                FILE_GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                null,
                                OPEN_EXISTING,
                                0,
                                null);

    if (handle != INVALID_HANDLE_VALUE) {

        USN_JOURNAL_DATA_V2 journal_data = { 0 };
        DWORD bytes_read = 0;

        bool device_io_result = DeviceIoControl(handle,
                                                FSCTL_QUERY_USN_JOURNAL,
                                                null,
                                                0,
                                                &journal_data,
                                                sizeof(journal_data),
                                                &bytes_read,
                                                null);

        if (device_io_result) {
            if ((database->latest_usn != journal_data.NextUsn) ||
                (database->latest_journal_id != journal_data.UsnJournalID)) {

                READ_USN_JOURNAL_DATA_V1 read_data_cmd = { 0 };
                read_data_cmd.StartUsn = database->latest_usn;
                read_data_cmd.ReasonMask = USN_REASON_FILE_CREATE | USN_REASON_FILE_DELETE | USN_REASON_RENAME_NEW_NAME;
                read_data_cmd.ReturnOnlyOnClose = 0;
                read_data_cmd.Timeout = 1;
                read_data_cmd.BytesToWaitFor = 4096;
                read_data_cmd.UsnJournalID = database->latest_journal_id;
                read_data_cmd.MinMajorVersion = 3;
                read_data_cmd.MaxMajorVersion = 3;

                u64 read_buffer_size = KILOBYTES(64);
                u8 *read_buffer = buffer_append(buffer, read_buffer_size);
                if (read_buffer) {
                    bool device_io_result = DeviceIoControl(handle,
                                                            FSCTL_READ_USN_JOURNAL,
                                                            &read_data_cmd,
                                                            sizeof(read_data_cmd),
                                                            read_buffer,
                                                            USN_BUFFER_SIZE,
                                                            &bytes_read,
                                                            null);
                    if (device_io_result) {
                        bool buffer_is_full = false;
                        u8 *buffer_end = read_buffer + bytes_read;
                        u8 *ptr        = read_buffer + sizeof(USN);

                        while (ptr < buffer_end && !buffer_is_full) {
                            USN_RECORD *usn_record = (USN_RECORD *)ptr;

                            if (usn_record->MajorVersion != 3) {
                                continue;
                            }

                            USN_RECORD_V3 *usn_record_v3 = (USN_RECORD_V3 *)ptr;
                            change *change = buffer_append(buffer, sizeof(*change));
                            if (!change) {
                                buffer_is_full = true;
                                continue;
                            }

                            zero_struct(change);

                            change_type change_type = (usn_record_v3->Reason & USN_REASON_FILE_DELETE ? CHANGE_TYPE_DELETE :
                                                       usn_record_v3->Reason & USN_REASON_FILE_CREATE ? CHANGE_TYPE_INSERT :
                                                       CHANGE_TYPE_UPDATE);

                            change->usn          = usn_record_v3->Usn;
                            change->type         = change_type;
                            change->id           = ntfs_id64_from_id128(usn_record_v3->FileReferenceNumber);
                            change->parent_id    = ntfs_id64_from_id128(usn_record_v3->ParentFileReferenceNumber);
                            change->wname_length = usn_record_v3->FileNameLength / 2;
                            change->attributes   = usn_record_v3->FileAttributes;
                            change->ignore       = false;

                            change->wname = buffer_append(buffer, usn_record_v3->FileNameLength);
                            if (!change->wname) {
                                buffer_is_full = true;
                                continue;
                            }

                            memcpy(change->wname, usn_record_v3->FileName, usn_record_v3->FileNameLength);

                            change_list_add(&changes, change);

                            ptr += usn_record->RecordLength;
                        }
                    } else {
                        debug_log_error_win32("DeviceIoControl");
                    }
                } else {
                    debug_log_error("Could not allocate buffer for usn query.");
                }
            }
        } else {
            debug_log_error_win32("DeviceIoControl");
        }
    } else {
        debug_log_error_win32("CreateFileA");
    }

    CloseHandle(handle);
    ntfs_usn_mark_ignore(changes);

    return changes;
}
