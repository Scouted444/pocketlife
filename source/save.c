// PocketLife - save to the SD card (flashcart / emulator) with libfat
#include "game.h"
#include <stdio.h>
#include <string.h>

#if defined(__has_include)
#if __has_include(<fat.h>)
#include <fat.h>
#define HAVE_FAT 1
#endif
#endif

#define SAVE_MAGIC 0x504C4946u   // "PLIF"
#define SAVE_VER   2
#define SAVE_PATH  "/pocketlife.sav"

typedef struct { u32 magic, version, size; } SaveHdr;

static bool s_fat;

bool save_init(void) {
#ifdef HAVE_FAT
    s_fat = fatInitDefault();
#endif
    return s_fat;
}

bool save_exists(void) {
    if (!s_fat) return false;
    FILE* f = fopen(SAVE_PATH, "rb");
    if (!f) return false;
    SaveHdr h = { 0 };
    bool ok = fread(&h, sizeof(h), 1, f) == 1 && h.magic == SAVE_MAGIC && h.version == SAVE_VER && h.size == sizeof(World);
    fclose(f);
    return ok;
}

bool save_write(void) {
    if (!s_fat) return false;
    FILE* f = fopen(SAVE_PATH, "wb");
    if (!f) return false;
    SaveHdr h = { SAVE_MAGIC, SAVE_VER, sizeof(World) };
    bool ok = fwrite(&h, sizeof(h), 1, f) == 1 && fwrite(&W, sizeof(World), 1, f) == 1 && fwrite(&OPT, sizeof(OPT), 1, f) == 1;
    fclose(f);
    return ok;
}

bool save_read(void) {
    if (!save_exists()) return false;
    FILE* f = fopen(SAVE_PATH, "rb");
    if (!f) return false;
    SaveHdr h;
    static World tmp;
    bool ok = fread(&h, sizeof(h), 1, f) == 1 && fread(&tmp, sizeof(World), 1, f) == 1;
    Options o;
    if (ok && fread(&o, sizeof(o), 1, f) == 1) OPT = o;
    fclose(f);
    if (!ok) return false;
    memcpy(&W, &tmp, sizeof(World));
    // nobody is mid-action after a load
    for (int i = 0; i < W.nobj; i++) W.obj[i].user = 0;
    for (int i = 0; i < W.nsim; i++) {
        Sim* s = &W.sim[i];
        if (s->state == SS_ACT && s->qlen) { s->x = s->homeX; s->z = s->homeZ; }
        s->qlen = 0; s->state = SS_IDLE; s->pose = POSE_STAND; s->y = 0; s->plen = 0;
    }
    if (OPT.speed == 0) OPT.speed = 1;
    return true;
}
