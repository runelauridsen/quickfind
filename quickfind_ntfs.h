// NOTE(rune): This file contains a NTFS master file table parser,
// which reads directly from disk, looks through each MFT record,
// out outputs a list of files in a volume.

// This file also contains a wrapper for winioctl.h USN calls,
// which can be used the monitor changes in a volume.

////////////////////////////////////////////////////////////////
// rune: References

// Master File Table:
// http://inform.pucp.edu.pe/~inf232/Ntfs/ntfs_doc_v0.5/index.html
// https://handmade.network/forums/articles/t/7002-tutorial_parsing_the_mft

// USN Journal:
// https://learn.microsoft.com/en-us/windows/win32/fileio/change-journals
// https://blog.trailofbits.com/2020/03/16/real-time-file-monitoring-on-windows-with-osquery/

////////////////////////////////////////////////////////////////
// rune: Output types

typedef enum ntfs_error ntfs_error;
enum ntfs_error {
    NTFS_ERROR_NONE,

    NTFS_ERROR_COULD_NOT_OPEN_VOLUME,                   // Error during CreateFile
    NTFS_ERROR_COULD_NOT_READ_BOOT_SECTOR,              // Error when trying to read boot sector
    NTFS_ERROR_COULD_NOT_READ_MFT_RECORD,               // Error when trying to read $MFT record
    NTFS_ERROR_DATA_ATTRIBUTE_NON_RESIDENT,             // Data attribute of the $MFT record is non resident
    NTFS_ERROR_COULD_NOT_READ_FROM_VOLUME,              // Error during ntfs_read_from_volume

    NTFS_ERROR_PARSE_RECORD_NO_MAGIC_NUMBER,            // Current record did not have NTFS_MAGIC_NUMBER in it's header
    NTFS_ERROR_PARSE_RECORD_NOT_IN_USE,                 // Current record is not marked as in use
    NTFS_ERROR_PASRE_FILE_NAME_ATTRIBUTE_MISSING,       // No file name attribute found on current record
    NTFS_ERROR_PARSE_FILE_NAME_ATTRIBUTE_NON_RESIDENT,  // File name attribute on current records is non resident

    NTFS_ERROR_COUNT
};

typedef struct ntfs_parsed_mft_record ntfs_parsed_mft_record;
struct ntfs_parsed_mft_record {
    ntfs_error   parse_error;
    record_id    id;
    record_id    parent_id;
    wchar       *name;
    u32          name_len;
    u32          attributes;
};

typedef struct ntfs_parsed_data_run ntfs_parsed_datarun;
struct ntfs_parsed_data_run {
    u64 length; // NOTE(rune): In bytes
    u64 offset; // NOTE(rune): Absolute offset in bytes.
};

typedef struct ntfs_mft_iterator ntfs_mft_iter;
struct ntfs_mft_iterator {
    ntfs_parsed_datarun data_runs[128];
    u32 data_run_count;

    u8 *buffer;
    u32 buffer_size;

    u32 bytes_per_sector;
    u32 bytes_per_cluster;

    u32 current_datarun;
    u64 current_offset_in_datarun;

    HANDLE volume;
};

typedef struct ntfs_usn_journal_data ntfs_usn_journal_data;
struct ntfs_usn_journal_data {
    u64 journal_id;
    u64 next_usn;
};

////////////////////////////////////////////////////////////////
// rune: Master file format

#define NTFS_MAGIC_NUMBER           0x454C4946  // "FILE" in ascii
#define NTFS_FILE_RECORD_SIZE       1024        // Size of a record in the master file table

#define USN_BUFFER_SIZE         KILOBYTES(64)

#pragma pack(push,1)
typedef struct ntfs_boot_sector ntfs_boot_sector;
struct ntfs_boot_sector {
    u8     jump[3];
    char   name[8];
    u16    bytes_per_sector;         // The number of bytes in a sector. This should be 512.
    u8     sectors_per_cluster;      // The number of sectors in a cluster. Clusters are used for less-granular access to the partition. They're usually 4KB.
    u16    reserved_sectors;
    u8     unused0[3];
    u16    unused1;
    u8     media;
    u16    unused2;
    u16    sectors_per_track;
    u16    heads_per_cylinder;
    u32    hidden_sectors;
    u32    unused3;
    u32    unused4;
    u64    total_sectors;
    u64    mft_start;                // The start of the MFT, given as a cluster index.
    u64    mft_mirror_start;
    u32    clusters_per_file_record;
    u32    clusters_per_index_block;
    u64    serial_number;
    u32    checksum;
    u8     bootloader[426];
    u16    boot_signature;
};


typedef struct ntfs_mft_record_header ntfs_mft_record_header;
struct ntfs_mft_record_header {
    u32    magic_number;
    u16    update_sequence_offset;
    u16    update_sequence_size;
    u64    log_sequence;
    u16    sequence_number;
    u16    hard_link_count;
    u16    first_attribute_offset;
    u16    is_in_use : 1;
    u16    is_directory : 1;
    u32    used_size;
    u32    allocated_size;
    u64    file_reference;
    u16    next_attribute_id;
    u16    unused;
    u32    record_number;
};

typedef union ntfs_mft_record ntfs_mft_record;
union ntfs_mft_record {
    ntfs_mft_record_header header;
    u8 bytes[1024];
};


typedef enum ntfs_attribute_type ntfs_attribute_type;
enum ntfs_attribute_type {
    NTFS_ATTRIBUTE_TYPE_STANDARD_INFORMATION = 0x10,
    NTFS_ATTRIBUTE_TYPE_ATTRIBUTE_LIST       = 0x20,
    NTFS_ATTRIBUTE_TYPE_FILE_NAME            = 0x30,
    NTFS_ATTRIBUTE_TYPE_OBJECT_ID            = 0x40,
    NTFS_ATTRIBUTE_TYPE_VOLUME_NAME          = 0x60,
    NTFS_ATTRIBUTE_TYPE_VOLUME_INFORMATION   = 0x70,
    NTFS_ATTRIBUTE_TYPE_DATA                 = 0x80,
    NTFS_ATTRIBUTE_TYPE_INDEX_ROOT           = 0x90,
    NTFS_ATTRIBUTE_TYPE_INDEX_ALLOCATION     = 0xA0,
    NTFS_ATTRIBUTE_TYPE_BITMAP               = 0xB0,
    NTFS_ATTRIBUTE_TYPE_REPARSE_POINT        = 0xC0,
    NTFS_ATTRIBUTE_TYPE_END                  = 0xFFFFFFFF
};

typedef enum ntfs_namespace ntfs_namespace;
enum ntfs_namespace {
    NTFS_NAMESPACE_POSIX         = 0x0,
    NTFS_NAMESPACE_WIN32         = 0x1,
    NTFS_NAMESPACE_DOS           = 0x2,
    NTFS_NAMESPACE_WIN32_AND_DOS = 0x3
};

typedef struct ntfs_attribute ntfs_attribute;
struct ntfs_attribute {
    u32    attribute_type;
    u32    attribute_size;
    u8     is_non_resident;
    u8     name_length;
    u16    name_offset;
    u16    flags;
    u16    attribute_id;
};

typedef struct ntfs_resident_attribute ntfs_resident_attribute;
struct ntfs_resident_attribute {
    ntfs_attribute header;
    u32    attribute_length;
    u16    attribute_offset;
    u8     indexed;
    u8     unused;
};

typedef struct ntfs_non_resident_attribute ntfs_non_resident_attribute;
struct ntfs_non_resident_attribute {
    ntfs_attribute header;
    u64    first_cluster;
    u64    last_cluster;
    u16    data_runs_offset;
    u16    compression_unit;
    u32    unused;
    u64    attribute_allocated;
    u64    attribute_size;
    u64    stream_data_size;
};

typedef struct ntfs_file_name_attribute ntfs_file_name_attribute;
struct ntfs_file_name_attribute {
    ntfs_resident_attribute header;
    u64    parent_record_number : 48;
    u64    parent_sequence_number : 16;
    u64    creation_time;
    u64    modification_time;
    u64    metadata_modification_time;
    u64    read_time;
    u64    allocated_size;
    u64    real_size;
    u32    flags;
    u32    repase;
    u8     file_name_length;
    u8     namespace;
    wchar  file_name[1];
};

typedef struct ntfs_data_run_header ntfs_datarun_header;
struct ntfs_data_run_header {
    u8     length_field_size : 4;
    u8     offset_field_size : 4;
    u8     data[1];
};

typedef struct ntfs_list_entry ntfs_list_entry;
struct ntfs_list_entry {
    u32 attribute_type;
    u16 entry_length;
    u8  name_length;
    u8  name_offset;
    u64 starting_vcn;
    u64 record_number;
    u16 attribute_id;
    wchar name[1];
};
#pragma pack(pop)

////////////////////////////////////////////////////////////////
// rune: Master file table parser

// rune: Helpers
static void ntfs_fixup_record(ntfs_mft_record_header *f);
static bool ntfs_read_from_volume(HANDLE volume, void *buffer, u32 size, u64 from);
static u8 * ntfs_next_datarun(u8 *datarun_start, u64 *length_in_clusters, u64 *offset_in_clusters);
static bool ntfs_next_attribute(ntfs_mft_record *record, ntfs_attribute **attribute);
static u64  ntfs_get_absolute_offset_of_record_number(u64 find_number, ntfs_parsed_datarun *dataruns, u32 datarun_count);

// NOTE(rune): Searches for a $FILE_NAME attribute and stores the file name attribute's data in pared_record.
// Sets pared_record.parse_error if the record could not be parsed.
static void ntfs_parse_mft_record(ntfs_mft_iter *iterator, ntfs_mft_record *record, ntfs_parsed_mft_record *parsed_record);

// NOTE(rune): Opens a handle to the drive_letter's volume, and parses the first MFT record,
// which is the record for the MFT itself, and stores the parsed data runs in iterator->data_runs.
// The data runs describe were the MFT data is stored on disk. The iterator should be closed
// with ntfs_file_record_iterator_close, even if this function returns an error.
static ntfs_error ntfs_mft_iter_open(ntfs_mft_iter *iter, char drive_letter, void *buffer, u32 buffer_size);
static bool       ntfs_mft_iter_advance(ntfs_mft_iter *iter, ntfs_parsed_mft_record *parsed_record);
static void       ntfs_mft_iter_close(ntfs_mft_iter *iterator);

////////////////////////////////////////////////////////////////
// rune: USN journal

// rune: Helpers
static void      ntfs_usn_mark_ignore(change_list changes);
static record_id ntfs_id64_from_id128(FILE_ID_128 id128);

static void      change_list_add(change_list *list, change *node);

// TODO(rune): Make this static use fixed sized buffer instead of dynbuffer_t.
// Maybe also add an UsnJournalIterator thing?
static change_list ntfs_get_usn_journal_changes(buffer *buffer, db *database);
static bool        ntfs_get_journal_data(ntfs_usn_journal_data *journal_data, char drive_letter);
