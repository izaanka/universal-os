#ifndef FAT32_H
#define FAT32_H

/* ============================================================
 * Kask OS — FAT32 Filesystem Driver (Header)
 * Buffer-based: works on any contiguous memory region
 * (ramdisk, GRUB module, or future disk sector cache)
 * ============================================================ */

#include <stdint.h>

/* ---- FAT32 Directory Attributes ---- */
#define FAT_ATTR_READONLY   0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LFN        0x0F   /* Long File Name entry */

#define FAT32_EOC           0x0FFFFFF8u  /* End of cluster chain */

/* ---- BIOS Parameter Block (packed, at offset 0 of volume) ---- */
typedef struct __attribute__((packed)) {
    uint8_t  jmp[3];
    uint8_t  oem[8];
    uint16_t bytes_per_sec;
    uint8_t  sec_per_clus;
    uint16_t rsvd_sec_cnt;
    uint8_t  num_fats;
    uint16_t root_ent_cnt;   /* Must be 0 for FAT32 */
    uint16_t tot_sec16;      /* Must be 0 for FAT32 */
    uint8_t  media;
    uint16_t fat_sz16;       /* Must be 0 for FAT32 */
    uint16_t sec_per_trk;
    uint16_t num_heads;
    uint32_t hidd_sec;
    uint32_t tot_sec32;
    uint32_t fat_sz32;
    uint16_t ext_flags;
    uint16_t fs_ver;
    uint32_t root_clus;
    uint16_t fs_info;
    uint16_t bk_boot_sec;
    uint8_t  reserved[12];
    uint8_t  drv_num;
    uint8_t  reserved1;
    uint8_t  boot_sig;
    uint32_t vol_id;
    uint8_t  vol_lab[11];
    uint8_t  fs_type[8];     /* "FAT32   " */
} fat32_bpb_t;

/* ---- 32-byte Short Directory Entry ---- */
typedef struct __attribute__((packed)) {
    uint8_t  name[11];       /* 8.3 padded with spaces */
    uint8_t  attr;
    uint8_t  nt_res;
    uint8_t  crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_acc_date;
    uint16_t fst_clus_hi;
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t fst_clus_lo;
    uint32_t file_size;
} fat32_dirent_t;

/* ---- 32-byte LFN Entry ---- */
typedef struct __attribute__((packed)) {
    uint8_t  ord;
    uint16_t name1[5];
    uint8_t  attr;           /* Always 0x0F */
    uint8_t  type;
    uint8_t  chksum;
    uint16_t name2[6];
    uint16_t fst_clus_lo;   /* Always 0 */
    uint16_t name3[2];
} fat32_lfn_t;

/* ---- Volume Mount State ---- */
typedef struct {
    const uint8_t* buf;
    uint32_t       buf_sz;
    uint32_t       bytes_per_sec;
    uint32_t       sec_per_clus;
    uint32_t       fat_start;    /* byte offset of FAT region */
    uint32_t       data_start;   /* byte offset of data region */
    uint32_t       root_clus;
    int            mounted;
    char           vol_label[12];
} fat32_vol_t;

/* ---- File Handle ---- */
typedef struct {
    fat32_vol_t* vol;
    uint32_t     first_clus;
    uint32_t     size;
    uint32_t     pos;
} fat32_file_t;

/* ---- Directory Iterator ---- */
typedef struct {
    fat32_vol_t* vol;
    uint32_t     clus;
    uint32_t     entry_idx;
    char         name[256];
    uint32_t     first_clus;
    uint32_t     size;
    uint8_t      attr;
    int          valid;
} fat32_dir_t;

/* ---- Public API ---- */
int  fat32_mount  (fat32_vol_t* vol, const uint8_t* buf, uint32_t sz);
int  fat32_opendir(fat32_dir_t* dir, fat32_vol_t* vol, const char* path);
int  fat32_readdir(fat32_dir_t* dir);
int  fat32_open   (fat32_file_t* f,  fat32_vol_t* vol, const char* path);
int  fat32_read   (fat32_file_t* f,  uint8_t* out, uint32_t len);
void fat32_print_info(fat32_vol_t* vol);

#endif /* FAT32_H */
