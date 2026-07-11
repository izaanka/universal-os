#include "exfat.h"
#include "kernel.h"
#include <stdint.h>

/* ============================================================
 * Kask OS — exFAT Filesystem Driver
 * ============================================================ */

/* ---- Little-endian helpers ---- */
static inline uint16_t xf_rd16(const uint8_t* p){
    return (uint16_t)(p[0]|((uint16_t)p[1]<<8));
}
static inline uint32_t xf_rd32(const uint8_t* p){
    return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static inline uint64_t xf_rd64(const uint8_t* p){
    return (uint64_t)xf_rd32(p)|((uint64_t)xf_rd32(p+4)<<32);
}

/* ---- Cluster → byte offset ---- */
static uint32_t xf_clus_off(const exfat_vol_t* v, uint32_t c){
    return v->heap_off + (c - 2u) * v->bytes_per_clus;
}

/* ---- FAT chain: next cluster ---- */
static uint32_t xf_fat_next(const exfat_vol_t* v, uint32_t c){
    uint32_t off = v->fat_off + c*4u;
    if(off+4u > v->buf_sz) return 0xFFFFFFFFu;
    return xf_rd32(v->buf + off);
}

/* ---- UTF-16LE → ASCII append ---- */
static int xf_utf16(char* dst, int pos, const uint16_t* src, int n){
    for(int i=0;i<n && pos<254;i++){
        uint16_t c=src[i];
        if(!c) break;
        dst[pos++]=(c<0x80u)?(char)c:'?';
    }
    return pos;
}

/* ============================================================
 * exfat_mount
 * ============================================================ */
int exfat_mount(exfat_vol_t* vol, const uint8_t* buf, uint32_t sz){
    k_memset(vol,0,sizeof(*vol));
    if(!buf||sz<512u) return -1;

    /* Check OEM signature "EXFAT   " at offset 3 */
    const uint8_t exfat_sig[8]={'E','X','F','A','T',' ',' ',' '};
    for(int i=0;i<8;i++) if(buf[3+i]!=exfat_sig[i]) return -1;

    const exfat_boot_t* bs = (const exfat_boot_t*)buf;

    uint8_t  bss = bs->bytes_per_sec_shift;
    uint8_t  scs = bs->sec_per_clus_shift;
    if(bss<9u||bss>12u) return -1; /* 512 – 4096 bytes/sector */

    uint32_t bps  = 1u << bss;
    uint32_t spc  = 1u << scs;
    uint32_t bpc  = bps * spc;

    vol->buf           = buf;
    vol->buf_sz        = sz;
    vol->bytes_per_sec = bps;
    vol->sec_per_clus  = spc;
    vol->bytes_per_clus= bpc;
    vol->fat_off       = xf_rd32((const uint8_t*)&bs->fat_offset) * bps;
    vol->heap_off      = xf_rd32((const uint8_t*)&bs->cluster_heap_offset) * bps;
    vol->root_clus     = xf_rd32((const uint8_t*)&bs->root_dir_cluster);
    vol->mounted       = 1;

    /* Try to read volume label from root dir */
    uint32_t c = vol->root_clus;
    int scanned = 0;
    while(c>=2u && c<0xFFFFFFF8u && scanned<64){
        uint32_t base = xf_clus_off(vol,c);
        uint32_t epc  = bpc/32u;
        for(uint32_t i=0;i<epc;i++){
            uint32_t eoff=base+i*32u;
            if(eoff+32u>sz) goto lbl_done;
            uint8_t type=buf[eoff];
            if(type==EXFAT_TYPE_EOD) goto lbl_done;
            if(type==EXFAT_TYPE_LABEL){
                uint8_t nchars=buf[eoff+1];
                int lp=0;
                const uint16_t* lname=(const uint16_t*)(buf+eoff+2);
                for(int li=0;li<nchars&&lp<255;li++){
                    uint16_t lc=lname[li];
                    vol->vol_label[lp++]=(lc<0x80u)?(char)lc:'?';
                }
                vol->vol_label[lp]='\0';
                goto lbl_done;
            }
            scanned++;
        }
        c=xf_fat_next(vol,c);
    }
    lbl_done:
    return 0;
}

/* ============================================================
 * Internal: open dir by cluster, then walk path
 * ============================================================ */
static int xf_find_in_dir(exfat_vol_t* v, uint32_t dir_clus,
                           const char* comp, uint32_t* out_clus, int* is_dir){
    uint32_t bpc = v->bytes_per_clus;
    uint32_t epc = bpc/32u;
    uint32_t c   = dir_clus;

    while(c>=2u && c<0xFFFFFFF8u){
        uint32_t base=xf_clus_off(v,c);
        for(uint32_t i=0;i<epc;){
            uint32_t eoff=base+i*32u;
            if(eoff+32u>v->buf_sz) return -1;
            uint8_t type=v->buf[eoff];
            if(type==EXFAT_TYPE_EOD) return -1;
            if(type!=EXFAT_TYPE_FILE){ i++; continue; }

            const exfat_file_entry_t* fe=(const exfat_file_entry_t*)(v->buf+eoff);
            uint8_t sec_cnt=fe->secondary_cnt;
            uint16_t attribs=xf_rd16((const uint8_t*)&fe->file_attribs);

            /* Read stream + name secondaries */
            uint32_t first_clus=0; int name_len=0; char name[256]; name[0]='\0';
            int np=0;
            for(uint8_t s=0;s<sec_cnt&&(i+1+s)<epc;s++){
                uint32_t soff=base+(i+1+s)*32u;
                if(soff+32u>v->buf_sz) break;
                uint8_t st=v->buf[soff];
                if(st==EXFAT_TYPE_STREAM){
                    const exfat_stream_entry_t* se=(const exfat_stream_entry_t*)(v->buf+soff);
                    name_len=se->name_length;
                    first_clus=xf_rd32((const uint8_t*)&se->first_cluster);
                } else if(st==EXFAT_TYPE_FNAME){
                    const exfat_name_entry_t* ne=(const exfat_name_entry_t*)(v->buf+soff);
                    np=xf_utf16(name,np,ne->file_name,15);
                }
            }
            name[np<name_len?np:name_len]='\0';
            (void)name_len;

            if(k_strcmp(name,comp)==0){
                *out_clus=first_clus;
                *is_dir  =(attribs&EXFAT_ATTR_DIRECTORY)?1:0;
                return 0;
            }
            i+=1+sec_cnt;
        }
        c=xf_fat_next(v,c);
    }
    return -1;
}

/* ============================================================
 * exfat_opendir
 * ============================================================ */
int exfat_opendir(exfat_dir_t* dir, exfat_vol_t* vol, const char* path){
    k_memset(dir,0,sizeof(*dir));
    dir->vol=vol; dir->valid=0;
    if(!vol->mounted) return -1;

    uint32_t clus=vol->root_clus;
    if(path && path[0]=='/') path++;

    while(path && *path){
        char comp[256]; int ci=0;
        while(*path && *path!='/' && ci<255) comp[ci++]=*path++;
        comp[ci]='\0';
        if(*path=='/') path++;
        if(!ci||(comp[0]=='.'&&comp[1]=='\0')) continue;

        uint32_t next_clus=0; int is_dir=0;
        if(xf_find_in_dir(vol,clus,comp,&next_clus,&is_dir)!=0) return -1;
        if(!is_dir) return -1;
        clus=next_clus;
    }

    dir->clus        = clus;
    dir->byte_in_clus= 0;
    dir->valid       = 1;
    return 0;
}

/* ============================================================
 * exfat_readdir — advance to next valid entry
 * ============================================================ */
int exfat_readdir(exfat_dir_t* dir){
    exfat_vol_t* v=dir->vol;
    if(!v||!dir->valid) return 0;
    uint32_t bpc=v->bytes_per_clus;
    uint32_t epc=bpc/32u;

    while(1){
        uint32_t c=dir->clus;
        if(c<2u||c>=0xFFFFFFF8u){dir->valid=0;return 0;}

        uint32_t base=xf_clus_off(v,c);
        uint32_t ei  =dir->byte_in_clus/32u;

        while(ei<epc){
            uint32_t eoff=base+ei*32u;
            if(eoff+32u>v->buf_sz){dir->valid=0;return 0;}
            uint8_t type=v->buf[eoff];

            if(type==EXFAT_TYPE_EOD){dir->valid=0;return 0;}
            if(type!=EXFAT_TYPE_FILE){ei++;continue;}

            const exfat_file_entry_t* fe=(const exfat_file_entry_t*)(v->buf+eoff);
            uint8_t sec_cnt=fe->secondary_cnt;
            dir->attribs=xf_rd16((const uint8_t*)&fe->file_attribs);

            char name[256]; int np=0; int name_len=0;
            uint32_t first_clus=0; uint64_t sz=0;

            for(uint8_t s=0;s<sec_cnt&&(ei+1+s)<epc;s++){
                uint32_t soff=base+(ei+1+s)*32u;
                if(soff+32u>v->buf_sz) break;
                uint8_t st=v->buf[soff];
                if(st==EXFAT_TYPE_STREAM){
                    const exfat_stream_entry_t* se=(const exfat_stream_entry_t*)(v->buf+soff);
                    name_len=se->name_length;
                    first_clus=xf_rd32((const uint8_t*)&se->first_cluster);
                    sz=xf_rd64((const uint8_t*)&se->data_length);
                } else if(st==EXFAT_TYPE_FNAME){
                    const exfat_name_entry_t* ne=(const exfat_name_entry_t*)(v->buf+soff);
                    np=xf_utf16(name,np,ne->file_name,15);
                }
            }
            if(np>name_len) np=name_len;
            name[np]='\0';
            k_strncpy(dir->name,name,255);
            dir->name[255]='\0';
            dir->first_clus=first_clus;
            dir->size      =sz;
            dir->byte_in_clus=(ei+1+sec_cnt)*32u;
            return 1;
        }

        /* Next cluster */
        dir->clus=xf_fat_next(v,c);
        dir->byte_in_clus=0;
        if(dir->clus>=0xFFFFFFF8u){dir->valid=0;return 0;}
    }
}

/* ============================================================
 * exfat_open
 * ============================================================ */
int exfat_open(exfat_file_t* f, exfat_vol_t* vol, const char* path){
    k_memset(f,0,sizeof(*f));
    if(!vol->mounted||!path) return -1;

    const char* fn=path;
    for(const char* p=path;*p;p++) if(*p=='/') fn=p+1;

    char dirpart[256]; int dplen=(int)(fn-path);
    if(dplen>255) dplen=255;
    k_memset(dirpart,0,256);
    for(int i=0;i<dplen;i++) dirpart[i]=path[i];

    exfat_dir_t dir;
    if(exfat_opendir(&dir,vol,dirpart)!=0) return -1;
    while(exfat_readdir(&dir)){
        if(k_strcmp(dir.name,fn)==0){
            f->vol=vol; f->first_clus=dir.first_clus;
            f->size=dir.size; f->pos=0;
            return 0;
        }
    }
    return -1;
}

/* ============================================================
 * exfat_read
 * ============================================================ */
int exfat_read(exfat_file_t* f, uint8_t* out, uint32_t len){
    if(!f||!f->vol||!out||!len) return 0;
    exfat_vol_t* v=f->vol;
    uint32_t bpc=v->bytes_per_clus;
    uint64_t remain=f->size-f->pos;
    if(len>remain) len=(uint32_t)remain;

    uint32_t read=0;
    uint32_t c=f->first_clus;

    /* Skip already-consumed clusters */
    uint32_t skip=(uint32_t)(f->pos/bpc);
    for(uint32_t i=0;i<skip&&c<0xFFFFFFF8u;i++) c=xf_fat_next(v,c);
    uint32_t oc=(uint32_t)(f->pos%bpc);

    while(read<len && c>=2u && c<0xFFFFFFF8u){
        uint32_t base=xf_clus_off(v,c);
        uint32_t avail=bpc-oc;
        uint32_t chunk=len-read;
        if(chunk>avail) chunk=avail;
        if(base+oc+chunk>v->buf_sz) chunk=v->buf_sz-(base+oc);
        if(!chunk) break;
        k_memcpy(out+read, v->buf+base+oc, (int)chunk);
        read+=chunk; oc=0;
        c=xf_fat_next(v,c);
    }
    f->pos+=read;
    return (int)read;
}

/* ============================================================
 * exfat_print_info
 * ============================================================ */
void exfat_print_info(exfat_vol_t* vol){
    if(!vol->mounted){hw_print("exFAT: not mounted\n");return;}
    char buf[32];
    hw_print("  exFAT volume");
    if(vol->vol_label[0]){hw_print(" \"");hw_print(vol->vol_label);hw_print("\"");}
    hw_print("\n  BytesPerSec: ");
    k_itoa((int)vol->bytes_per_sec,buf,10);hw_print(buf);
    hw_print("  SecPerClus: ");
    k_itoa((int)vol->sec_per_clus,buf,10);hw_print(buf);
    hw_print("\n  RootClus: 0x");
    k_itoa((int)vol->root_clus,buf,16);hw_print(buf);
    hw_print("\n");
}
