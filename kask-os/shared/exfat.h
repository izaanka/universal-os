#ifndef EXFAT_H
#define EXFAT_H

/* ============================================================
 * Kask OS — exFAT Filesystem Driver (Header)
 * ============================================================ */

#include <stdint.h>

/* ---- Directory entry type codes ---- */
#define EXFAT_TYPE_EOD      0x00u   /* End of directory */
#define EXFAT_TYPE_BITMAP   0x81u   /* Allocation bitmap */
#define EXFAT_TYPE_UPCASE   0x82u   /* Up-case table */
#define EXFAT_TYPE_LABEL    0x83u   /* Volume label */
#define EXFAT_TYPE_FILE     0x85u   /* File primary entry */
#define EXFAT_TYPE_STREAM   0xC0u   /* Stream extension */
#define EXFAT_TYPE_FNAME    0xC1u   /* File name extension */

/* ---- File attributes ---- */
#define EXFAT_ATTR_READONLY  0x0001u
#define EXFAT_ATTR_HIDDEN    0x0002u
#define EXFAT_ATTR_SYSTEM    0x0004u
#define EXFAT_ATTR_DIRECTORY 0x0010u
#define EXFAT_ATTR_ARCHIVE   0x0020u

/* ---- Boot Sector (first 512 bytes of volume) ---- */
typedef struct __attribute__((packed)) {
    uint8_t  jmp[3];                /* EB 76 90 */
    uint8_t  oem[8];                /* "EXFAT   " */
    uint8_t  reserved[53];
    uint64_t partition_offset;
    uint64_t volume_length;         /* sectors */
    uint32_t fat_offset;            /* sectors from volume start */
    uint32_t fat_length;            /* sectors */
    uint32_t cluster_heap_offset;   /* sectors */
    uint32_t cluster_count;
    uint32_t root_dir_cluster;
    uint32_t volume_serial;
    uint16_t fs_revision;           /* 0x0100 */
    uint16_t volume_flags;
    uint8_t  bytes_per_sec_shift;   /* log2(bytes per sector) */
    uint8_t  sec_per_clus_shift;    /* log2(sectors per cluster) */
    uint8_t  num_fats;
    uint8_t  drive_select;
    uint8_t  percent_in_use;
    uint8_t  reserved2[7];
    /* boot code + signature omitted */
} exfat_boot_t;

/* ---- Generic 32-byte directory entry ---- */
typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t data[31];
} exfat_raw_entry_t;

/* ---- File primary entry (type 0x85) ---- */
typedef struct __attribute__((packed)) {
    uint8_t  type;          /* 0x85 */
    uint8_t  secondary_cnt;
    uint16_t set_checksum;
    uint16_t file_attribs;
    uint16_t reserved1;
    uint32_t create_time;
    uint32_t modified_time;
    uint32_t access_time;
    uint8_t  create_10ms;
    uint8_t  modified_10ms;
    uint8_t  create_utc;
    uint8_t  modified_utc;
    uint8_t  access_utc;
    uint8_t  reserved2[7];
} exfat_file_entry_t;

/* ---- Stream extension entry (type 0xC0) ---- */
typedef struct __attribute__((packed)) {
    uint8_t  type;          /* 0xC0 */
    uint8_t  general_sec_flags;
    uint8_t  reserved1;
    uint8_t  name_length;   /* characters */
    uint16_t name_hash;
    uint16_t reserved2;
    uint64_t valid_data_len;
    uint32_t reserved3;
    uint32_t first_cluster;
    uint64_t data_length;
} exfat_stream_entry_t;

/* ---- File name entry (type 0xC1) ---- */
typedef struct __attribute__((packed)) {
    uint8_t  type;          /* 0xC1 */
    uint8_t  general_sec_flags;
    uint16_t file_name[15]; /* UTF-16LE */
} exfat_name_entry_t;

/* ---- Volume mount state ---- */
typedef struct {
    const uint8_t* buf;
    uint32_t       buf_sz;
    uint32_t       bytes_per_sec;
    uint32_t       sec_per_clus;
    uint32_t       bytes_per_clus;
    uint32_t       fat_off;        /* byte offset of FAT */
    uint32_t       heap_off;       /* byte offset of cluster heap */
    uint32_t       root_clus;
    int            mounted;
    char           vol_label[256];
} exfat_vol_t;

/* ---- File handle ---- */
typedef struct {
    exfat_vol_t* vol;
    uint32_t     first_clus;
    uint64_t     size;
    uint64_t     pos;
} exfat_file_t;

/* ---- Directory iterator ---- */
typedef struct {
    exfat_vol_t* vol;
    uint32_t     clus;
    uint32_t     byte_in_clus;
    char         name[256];
    uint32_t     first_clus;
    uint64_t     size;
    uint16_t     attribs;
    int          valid;
} exfat_dir_t;

/* ---- Public API ---- */
int  exfat_mount  (exfat_vol_t* vol, const uint8_t* buf, uint32_t sz);
int  exfat_opendir(exfat_dir_t* dir, exfat_vol_t* vol, const char* path);
int  exfat_readdir(exfat_dir_t* dir);
int  exfat_open   (exfat_file_t* f,  exfat_vol_t* vol, const char* path);
int  exfat_read   (exfat_file_t* f,  uint8_t* out, uint32_t len);
void exfat_print_info(exfat_vol_t* vol);

#endif /* EXFAT_H */
