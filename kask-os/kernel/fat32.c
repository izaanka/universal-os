#include "fat32.h"
#include "kernel.h"
#include <stdint.h>

/* ============================================================
 * Kask OS — FAT32 Filesystem Driver
 * ============================================================ */

/* ---- Little-endian read helpers ---- */
static inline uint16_t f32_rd16(const uint8_t* p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static inline uint32_t f32_rd32(const uint8_t* p) {
    return p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}

/* ---- Cluster number → byte offset in buffer ---- */
static uint32_t clus_off(const fat32_vol_t* v, uint32_t c) {
    return v->data_start + (c - 2u) * v->sec_per_clus * v->bytes_per_sec;
}

/* ---- Read next cluster from FAT ---- */
static uint32_t fat_next(const fat32_vol_t* v, uint32_t c) {
    uint32_t off = v->fat_start + c * 4u;
    if (off + 4u > v->buf_sz) return 0x0FFFFFFFu;
    return f32_rd32(v->buf + off) & 0x0FFFFFFFu;
}

/* ---- 8.3 short name → null-terminated string ---- */
static void parse83(const uint8_t* raw, char* out) {
    int o = 0;
    for (int i = 0; i < 8 && raw[i] != ' '; i++) out[o++] = (char)raw[i];
    if (raw[8] != ' ') {
        out[o++] = '.';
        for (int i = 8; i < 11 && raw[i] != ' '; i++) out[o++] = (char)raw[i];
    }
    out[o] = '\0';
}

/* ---- Append UTF-16LE chars as ASCII (strip high byte) ---- */
static int utf16_append(char* dst, int pos, const uint16_t* src, int n) {
    for (int i = 0; i < n && pos < 254; i++) {
        uint16_t c = src[i];
        if (c == 0 || c == 0xFFFFu) break;
        dst[pos++] = (c < 0x80u) ? (char)c : '?';
    }
    return pos;
}

/* ============================================================
 * fat32_mount — validate BPB and populate vol state
 * Returns 0 on success, -1 on failure.
 * ============================================================ */
int fat32_mount(fat32_vol_t* vol, const uint8_t* buf, uint32_t sz) {
    k_memset(vol, 0, sizeof(*vol));
    if (!buf || sz < 512u) return -1;

    /* Boot signature */
    if (buf[510] != 0x55 || buf[511] != 0xAAu) return -1;

    const fat32_bpb_t* b = (const fat32_bpb_t*)buf;

    uint16_t bps = f32_rd16((const uint8_t*)&b->bytes_per_sec);
    uint8_t  spc = b->sec_per_clus;
    uint16_t rsc = f32_rd16((const uint8_t*)&b->rsvd_sec_cnt);
    uint8_t  nf  = b->num_fats;
    uint32_t fsz = f32_rd32((const uint8_t*)&b->fat_sz32);
    uint32_t rc  = f32_rd32((const uint8_t*)&b->root_clus);

    if (!bps || !spc || !rsc || !nf || !fsz) return -1;
    /* FAT32 requires root_ent_cnt == 0 and fat_sz16 == 0 */
    if (f32_rd16((const uint8_t*)&b->root_ent_cnt) != 0) return -1;
    if (f32_rd16((const uint8_t*)&b->fat_sz16)     != 0) return -1;
    /* "FAT" signature in fs_type field */
    if (b->fs_type[0]!='F'||b->fs_type[1]!='A'||b->fs_type[2]!='T') return -1;

    vol->buf          = buf;
    vol->buf_sz       = sz;
    vol->bytes_per_sec = bps;
    vol->sec_per_clus  = spc;
    vol->fat_start    = (uint32_t)rsc * bps;
    vol->data_start   = ((uint32_t)rsc + (uint32_t)nf * fsz) * bps;
    vol->root_clus    = rc;
    vol->mounted      = 1;

    /* Copy volume label (11 bytes, trim spaces) */
    int li = 0;
    for (int i = 0; i < 11 && b->vol_lab[i] != ' '; i++)
        vol->vol_label[li++] = (char)b->vol_lab[i];
    vol->vol_label[li] = '\0';

    return 0;
}

/* ============================================================
 * fat32_opendir — begin iterating a directory by path
 * path="" or "/" opens the root directory.
 * ============================================================ */
int fat32_opendir(fat32_dir_t* dir, fat32_vol_t* vol, const char* path) {
    k_memset(dir, 0, sizeof(*dir));
    dir->vol   = vol;
    dir->valid = 0;
    if (!vol->mounted) return -1;

    /* Start at root */
    uint32_t clus = vol->root_clus;

    /* Walk path components */
    if (path && path[0] == '/') path++;
    while (path && *path) {
        /* Extract next component */
        char comp[64]; int ci = 0;
        while (*path && *path != '/' && ci < 63) comp[ci++] = *path++;
        comp[ci] = '\0';
        if (*path == '/') path++;
        if (!ci || (comp[0]=='.'&&comp[1]=='\0')) continue;

        /* Search this cluster chain for comp */
        uint32_t bpc = vol->sec_per_clus * vol->bytes_per_sec;
        uint32_t epc = bpc / 32u;
        uint32_t found_clus = 0;
        int      found = 0;

        uint32_t c = clus;
        while (c >= 2u && c < FAT32_EOC && !found) {
            uint32_t base = clus_off(vol, c);
            for (uint32_t ei = 0; ei < epc && !found; ei++) {
                uint32_t eoff = base + ei*32u;
                if (eoff+32u > vol->buf_sz) break;
                const uint8_t* raw = vol->buf + eoff;
                if (raw[0] == 0x00u) goto done_search;
                if (raw[0] == 0xE5u) continue;
                uint8_t attr = raw[11];
                if ((attr & 0x3F) == FAT_ATTR_LFN) continue;
                if (!(attr & FAT_ATTR_DIRECTORY)) continue;
                char sn[13]; parse83(raw, sn);
                if (k_strcmp(sn, comp) == 0) {
                    uint32_t hi = f32_rd16(raw+20);
                    uint32_t lo = f32_rd16(raw+26);
                    found_clus = (hi<<16)|lo;
                    found = 1;
                }
            }
            c = fat_next(vol, c);
        }
        done_search:
        if (!found) return -1;
        clus = found_clus;
    }

    dir->clus      = clus;
    dir->entry_idx = 0;
    dir->valid     = 1;
    return 0;
}

/* ============================================================
 * fat32_readdir — advance dir to next valid entry.
 * Returns 1 if entry was read, 0 at end-of-directory.
 * Fills dir->name, dir->attr, dir->first_clus, dir->size.
 * ============================================================ */
int fat32_readdir(fat32_dir_t* dir) {
    fat32_vol_t* v = dir->vol;
    if (!v || !dir->valid) return 0;

    uint32_t bpc = v->sec_per_clus * v->bytes_per_sec;
    uint32_t epc = bpc / 32u;

    /* LFN accumulation buffer — built forward by tracking seq number */
    char lfn[256];
    int  lfn_len = 0;
    int  has_lfn = 0;

    while (1) {
        uint32_t c = dir->clus;
        if (c < 2u || c >= FAT32_EOC) { dir->valid = 0; return 0; }

        uint32_t base = clus_off(v, c);

        while (dir->entry_idx < epc) {
            uint32_t eoff = base + dir->entry_idx * 32u;
            dir->entry_idx++;
            if (eoff + 32u > v->buf_sz) { dir->valid = 0; return 0; }

            const uint8_t* raw = v->buf + eoff;
            if (raw[0] == 0x00u) { dir->valid = 0; return 0; } /* EOD */
            if (raw[0] == 0xE5u) { has_lfn = 0; lfn_len = 0; continue; }

            uint8_t attr = raw[11];

            /* LFN entry */
            if ((attr & 0x3Fu) == FAT_ATTR_LFN) {
                const fat32_lfn_t* lf = (const fat32_lfn_t*)raw;
                if (lf->ord & 0x40u) { /* first LFN entry (last in seq) */
                    has_lfn = 1; lfn_len = 0;
                }
                /* Accumulate: LFN entries arrive in reverse order,
                   so prepend each block into a temp then reverse at end.
                   Simpler: collect and overwrite since we get last-first. */
                char tmp[14]; int tp = 0;
                tp = utf16_append(tmp, tp, lf->name1, 5);
                tp = utf16_append(tmp, tp, lf->name2, 6);
                tp = utf16_append(tmp, tp, lf->name3, 2);
                tmp[tp] = '\0';
                /* Prepend this segment */
                char merged[256];
                k_strcpy(merged, tmp);
                k_strcat(merged, lfn);
                k_strcpy(lfn, merged);
                lfn_len = k_strlen(lfn);
                continue;
            }

            /* Skip volume ID */
            if (attr & FAT_ATTR_VOLUME_ID) { has_lfn=0; lfn_len=0; continue; }
            /* Skip . and .. */
            if (raw[0] == '.') { has_lfn=0; lfn_len=0; continue; }

            /* Regular entry — copy name */
            if (has_lfn && lfn_len > 0) {
                k_strncpy(dir->name, lfn, 255);
                dir->name[255] = '\0';
            } else {
                parse83(raw, dir->name);
            }
            has_lfn = 0; lfn_len = 0;
            k_memset(lfn, 0, sizeof(lfn));

            dir->attr      = attr;
            uint32_t hi    = f32_rd16(raw+20);
            uint32_t lo    = f32_rd16(raw+26);
            dir->first_clus = (hi<<16)|lo;
            dir->size      = f32_rd32(raw+28);
            return 1;
        }

        /* Advance to next cluster */
        dir->clus = fat_next(v, c);
        dir->entry_idx = 0;
        if (dir->clus >= FAT32_EOC) { dir->valid = 0; return 0; }
    }
}

/* ============================================================
 * fat32_open — open a file by absolute path
 * ============================================================ */
int fat32_open(fat32_file_t* f, fat32_vol_t* vol, const char* path) {
    k_memset(f, 0, sizeof(*f));
    if (!vol->mounted || !path) return -1;

    /* Split into directory and filename */
    const char* fn = path;
    const char* p  = path;
    while (*p) { if (*p=='/') fn = p+1; p++; }

    /* Find dir part */
    char dirpart[256];
    int dplen = (int)(fn - path);
    if (dplen > 255) dplen = 255;
    k_memset(dirpart,0,256);
    for(int i=0;i<dplen;i++) dirpart[i]=path[i];

    fat32_dir_t dir;
    if (fat32_opendir(&dir, vol, dirpart) != 0) return -1;

    while (fat32_readdir(&dir)) {
        if (k_strcmp(dir.name, fn) == 0) {
            f->vol        = vol;
            f->first_clus = dir.first_clus;
            f->size       = dir.size;
            f->pos        = 0;
            return 0;
        }
    }
    return -1;
}

/* ============================================================
 * fat32_read — read len bytes from file into out
 * Returns number of bytes actually read.
 * ============================================================ */
int fat32_read(fat32_file_t* f, uint8_t* out, uint32_t len) {
    if (!f || !f->vol || !out || !len) return 0;
    fat32_vol_t* v = f->vol;
    uint32_t bpc = v->sec_per_clus * v->bytes_per_sec;
    uint32_t remain = f->size - f->pos;
    if (len > remain) len = remain;

    uint32_t read = 0;
    uint32_t c    = f->first_clus;

    /* Skip clusters already consumed */
    uint32_t skip = f->pos / bpc;
    for (uint32_t i = 0; i < skip && c < FAT32_EOC; i++)
        c = fat_next(v, c);

    uint32_t clus_off_in = f->pos % bpc;

    while (read < len && c >= 2u && c < FAT32_EOC) {
        uint32_t base  = clus_off(v, c);
        uint32_t avail = bpc - clus_off_in;
        uint32_t chunk = len - read;
        if (chunk > avail) chunk = avail;
        if (base + clus_off_in + chunk > v->buf_sz)
            chunk = v->buf_sz - (base + clus_off_in);
        if (!chunk) break;
        k_memcpy(out + read, v->buf + base + clus_off_in, (int)chunk);
        read       += chunk;
        clus_off_in = 0;
        c = fat_next(v, c);
    }
    f->pos += read;
    return (int)read;
}

/* ============================================================
 * fat32_print_info — print volume info to VGA console
 * ============================================================ */
void fat32_print_info(fat32_vol_t* vol) {
    if (!vol->mounted) { hw_print("FAT32: not mounted\n"); return; }
    char buf[32];
    hw_print("  FAT32 volume");
    if (vol->vol_label[0]) { hw_print(" \""); hw_print(vol->vol_label); hw_print("\""); }
    hw_print("\n  BytesPerSec: ");
    k_itoa((int)vol->bytes_per_sec, buf, 10); hw_print(buf);
    hw_print("  SecPerClus: ");
    k_itoa((int)vol->sec_per_clus, buf, 10);  hw_print(buf);
    hw_print("\n  RootClus: ");
    k_itoa((int)vol->root_clus, buf, 16); hw_print("0x"); hw_print(buf);
    hw_print("\n");
}
