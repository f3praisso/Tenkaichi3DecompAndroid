/* iOS: the game's data from the user's own disc image (the part that has no screen: ios_setup_ui.c has it).

   The engine reads the unpacked disc from <Documents>/gamedata (what port/tools/extract_disc.py makes on a PC, and what
   the Android launcher makes): disc/SLUS_216.78, disc/BIN/..., the other files of DATA under disc/DATA/, and every AFS
   archive of DATA split into gamedata/<name>/00000.bin, 00001.bin... one file per entry.

   There is no PC here: this reads the .iso (ISO 9660, 2048-byte sectors) the user pointed to and writes that layout.
   It runs on a thread of its own, and tells its progress through an IosProgress the screen reads. Nothing from the disc
   is kept anywhere but the app's own folder. */
#include <dirent.h>
#include <errno.h>
#include <libgen.h>
#include <mach-o/dyld.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ios_setup.h"

#define SECTOR 2048u
#define say(...) fprintf(stderr, __VA_ARGS__)

typedef struct {
    uint32_t lba, size;
    int dir;
    char name[256];
} Ent;

typedef struct {
    int afs;          /* split into gamedata/<stem>/ instead of copied */
    uint32_t lba, size;
    char path[512];   /* destination file, or the folder for an archive */
} Task;

static FILE *sIso;
static IosProgress *sP;

static void progress(const char *label) {
    snprintf(sP->what, sizeof(sP->what), "%s", label);
}

/* ---------------------------------------------------------------------------------------------------- files */
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int read_at(uint64_t off, void *buf, size_t n) {
    return fseeko(sIso, (off_t)off, SEEK_SET) == 0 && fread(buf, 1, n, sIso) == n;
}

static void mkdirs(const char *path) {
    char tmp[1024];
    char *p;

    snprintf(tmp, sizeof(tmp), "%s", path);
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

/* bytes [off, off+size) of the disc -> a file (a new file each time) */
static int copy_out(uint64_t off, uint64_t size, const char *dst, const char *label) {
    static uint8_t buf[1 << 20];
    FILE *f = fopen(dst, "wb");
    uint64_t done = 0;

    if (f == NULL) {
        snprintf(sP->fail, sizeof(sP->fail), "Não consegui criar %s (%s).", dst, strerror(errno));
        return 0;
    }
    while (done < size) {
        size_t n = size - done < sizeof(buf) ? (size_t)(size - done) : sizeof(buf);
        if (!read_at(off + done, buf, n) || fwrite(buf, 1, n, f) != n) {
            snprintf(sP->fail, sizeof(sP->fail), "Erro ao ler o disco ou gravar %s (%s). Falta espaço?", dst, strerror(errno));
            fclose(f);
            return 0;
        }
        done += n;
        sP->done += n;
        progress(label);
    }
    return fclose(f) == 0;
}

/* ---------------------------------------------------------------------------------------------------- ISO 9660 */
static Ent *list_dir(uint32_t lba, uint32_t size, int *count) {
    uint8_t *buf = malloc(size);
    Ent *v = NULL;
    uint32_t off = 0;
    int n = 0, cap = 0;

    *count = 0;
    if (buf == NULL || !read_at((uint64_t)lba * SECTOR, buf, size)) {
        free(buf);
        return NULL;
    }
    while (off < size) {
        const uint8_t *r = buf + off;
        unsigned len = r[0], nl;
        if (len == 0) { /* records do not cross a sector: the rest of this one is padding */
            off = (off / SECTOR + 1) * SECTOR;
            continue;
        }
        if (off + len > size) {
            break;
        }
        nl = r[32];
        if (!(nl == 1 && (r[33] == 0 || r[33] == 1))) {
            Ent *e;
            if (n == cap) {
                cap = cap ? cap * 2 : 64;
                v = realloc(v, (size_t)cap * sizeof(Ent));
            }
            e = &v[n++];
            e->lba = rd32(r + 2);
            e->size = rd32(r + 10);
            e->dir = (r[25] & 2) != 0;
            memcpy(e->name, r + 33, nl);
            e->name[nl] = 0;
            {   /* "NAME.EXT;1" -> "NAME.EXT" ; "NAME." -> "NAME" */
                char *semi = strchr(e->name, ';');
                if (semi) *semi = 0;
                nl = (unsigned)strlen(e->name);
                if (nl > 0 && e->name[nl - 1] == '.') e->name[nl - 1] = 0;
            }
        }
        off += len;
    }
    free(buf);
    *count = n;
    return v;
}

static const Ent *find(const Ent *v, int n, const char *name) {
    int i;
    for (i = 0; i < n; i++) {
        if (strcasecmp(v[i].name, name) == 0) {
            return &v[i];
        }
    }
    return NULL;
}

static Task *sTasks;
static int sNTasks, sCapTasks;

static Task *task_new(void) {
    if (sNTasks == sCapTasks) {
        sCapTasks = sCapTasks ? sCapTasks * 2 : 64;
        sTasks = realloc(sTasks, (size_t)sCapTasks * sizeof(Task));
    }
    memset(&sTasks[sNTasks], 0, sizeof(Task));
    return &sTasks[sNTasks++];
}

/* everything under a folder of the disc: files copied to <dst>/<name>, folders followed; in DATA, archives split */
static void plan_dir(const Ent *dir, const char *dst, const char *gamedata, int splitAfs) {
    int n = 0, i;
    Ent *v = list_dir(dir->lba, dir->size, &n);

    for (i = 0; i < n; i++) {
        Task *t;
        if (v[i].dir) {
            char sub[512];
            snprintf(sub, sizeof(sub), "%s/%s", dst, v[i].name);
            mkdirs(sub);
            plan_dir(&v[i], sub, gamedata, 0);
            continue;
        }
        t = task_new();
        t->lba = v[i].lba;
        t->size = v[i].size;
        if (splitAfs && strlen(v[i].name) > 4 && strcasecmp(v[i].name + strlen(v[i].name) - 4, ".AFS") == 0) {
            char stem[256];
            size_t k;
            snprintf(stem, sizeof(stem), "%s", v[i].name);
            stem[strlen(stem) - 4] = 0;
            for (k = 0; stem[k]; k++) {
                if (stem[k] >= 'A' && stem[k] <= 'Z') stem[k] = (char)(stem[k] + 32);
            }
            t->afs = 1;
            snprintf(t->path, sizeof(t->path), "%s/%s", gamedata, stem);
        } else {
            snprintf(t->path, sizeof(t->path), "%s/%s", dst, v[i].name);
        }
        sP->total += v[i].size;
    }
    free(v);
}

static int split_afs(const Task *t) {
    uint8_t head[8];
    uint8_t *table;
    uint32_t count, i;
    uint64_t base = (uint64_t)t->lba * SECTOR;

    if (!read_at(base, head, 8) || memcmp(head, "AFS\0", 4) != 0) {
        snprintf(sP->fail, sizeof(sP->fail), "%s não é um arquivo AFS válido.", t->path);
        return 0;
    }
    count = rd32(head + 4);
    if (count == 0 || count > 200000) {
        snprintf(sP->fail, sizeof(sP->fail), "Um arquivo AFS do disco tem %u entradas (estranho).", count);
        return 0;
    }
    table = malloc((size_t)count * 8);
    if (table == NULL || !read_at(base + 8, table, (size_t)count * 8)) {
        free(table);
        snprintf(sP->fail, sizeof(sP->fail), "Não consegui ler a tabela de %s.", t->path);
        return 0;
    }
    mkdirs(t->path);
    for (i = 0; i < count; i++) {
        uint32_t eoff = rd32(table + i * 8), esize = rd32(table + i * 8 + 4);
        char dst[640];
        if ((uint64_t)eoff + esize > (uint64_t)t->size) {
            snprintf(sP->fail, sizeof(sP->fail), "A entrada %u de %s está fora do arquivo.", i, t->path);
            free(table);
            return 0;
        }
        snprintf(dst, sizeof(dst), "%s/%05u.bin", t->path, i);
        if (!copy_out(base + eoff, esize, dst, strrchr(t->path, '/') + 1)) {
            free(table);
            return 0;
        }
    }
    free(table);
    return 1;
}

/* ---------------------------------------------------------------------------------------------------- the whole job */
static int extract(const char *isoPath, const char *gamedata);

/* Runs to the end (on the caller's thread) and tells how it went in `p`. */
int IosData_Extract(const char *isoPath, const char *gamedata, IosProgress *p) {
    int ok;

    sP = p;
    p->done = p->total = 0;
    p->what[0] = 0;
    p->fail[0] = 0;
    p->ok = 0;
    p->finished = 0;
    ok = extract(isoPath, gamedata);
    if (ok) {
        char ready[700];
        FILE *f;
        snprintf(ready, sizeof(ready), "%s/.bt3_ready", gamedata);
        if ((f = fopen(ready, "w")) != NULL) {
            fputs("ok\n", f);
            fclose(f);
        }
        say("bt3: the game's data are ready (%s can be deleted now)\n", isoPath);
    } else {
        say("bt3: unpacking failed: %s\n", p->fail);
    }
    if (ok) {
        p->done = p->total;
    }
    p->ok = ok;
    p->finished = 1;
    return ok;
}

static int extract(const char *isoPath, const char *gamedata) {
    uint8_t pvd[SECTOR];
    int n = 0, i;
    Ent *root;
    const Ent *slus, *bin, *data;
    char disc[512], p[640];
    struct statfs fs;

    sIso = fopen(isoPath, "rb");
    if (sIso == NULL) {
        snprintf(sP->fail, sizeof(sP->fail), "Não consegui abrir %s.", isoPath);
        return 0;
    }
    if (!read_at(16 * (uint64_t)SECTOR, pvd, SECTOR) || pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5) != 0) {
        snprintf(sP->fail, sizeof(sP->fail), "Isso não é uma imagem ISO 9660 de 2048 bytes por setor.");
        fclose(sIso);
        return 0;
    }
    root = list_dir(rd32(pvd + 156 + 2), rd32(pvd + 156 + 10), &n);
    if (root == NULL || (slus = find(root, n, "SLUS_216.78")) == NULL || (bin = find(root, n, "BIN")) == NULL ||
        (data = find(root, n, "DATA")) == NULL) {
        snprintf(sP->fail, sizeof(sP->fail), "Este disco não é o do jogo (USA, SLUS-21678): faltam SLUS_216.78, BIN ou DATA.");
        free(root);
        fclose(sIso);
        return 0;
    }
    snprintf(disc, sizeof(disc), "%s/disc", gamedata);
    mkdirs(disc);
    snprintf(p, sizeof(p), "%s/DATA", disc);
    mkdirs(p);
    snprintf(p, sizeof(p), "%s/BIN", disc);
    mkdirs(p);

    sNTasks = 0;
    sP->done = sP->total = 0;
    {   /* SLUS_216.78 */
        Task *t = task_new();
        t->lba = slus->lba;
        t->size = slus->size;
        snprintf(t->path, sizeof(t->path), "%s/SLUS_216.78", disc);
        sP->total += slus->size;
    }
    snprintf(p, sizeof(p), "%s/BIN", disc);
    plan_dir(bin, p, gamedata, 0);
    snprintf(p, sizeof(p), "%s/DATA", disc);
    plan_dir(data, p, gamedata, 1);
    free(root);

    if (statfs(gamedata, &fs) == 0 && (uint64_t)fs.f_bavail * fs.f_bsize < sP->total + (200ull << 20)) {
        snprintf(sP->fail, sizeof(sP->fail), "Falta espaço: preciso de %llu MB livres e há %llu MB.", (unsigned long long)((sP->total >> 20) + 200),
                 (unsigned long long)(((uint64_t)fs.f_bavail * fs.f_bsize) >> 20));
        fclose(sIso);
        return 0;
    }
    say("bt3: unpacking %s: %d files, %llu MB\n", isoPath, sNTasks, (unsigned long long)(sP->total >> 20));
    for (i = 0; i < sNTasks; i++) {
        const Task *t = &sTasks[i];
        int ok = t->afs ? split_afs(t) : copy_out((uint64_t)t->lba * SECTOR, t->size, t->path, strrchr(t->path, '/') + 1);
        if (!ok) {
            fclose(sIso);
            return 0;
        }
    }
    fclose(sIso);
    return 1;
}

static int exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

/* The first .iso in the folder, whose size has stopped changing (the Files app may still be copying it). */
int IosData_FindIso(const char *dir, char *out, size_t cap) {
    DIR *d = opendir(dir);
    struct dirent *e;
    int found = 0;

    if (d == NULL) {
        return 0;
    }
    while (!found && (e = readdir(d)) != NULL) {
        size_t n = strlen(e->d_name);
        if (n > 4 && strcasecmp(e->d_name + n - 4, ".iso") == 0) {
            snprintf(out, cap, "%s/%s", dir, e->d_name);
            found = 1;
        }
    }
    closedir(d);
    return found;
}

/* The engine's menu wording (port/translations in the source tree) comes inside the app; the engine reads it from
   <gamedata>/game-text/translations. Copied when it is not there yet (a file already there is left alone). */
static void copy_tree(const char *src, const char *dst) {
    DIR *d = opendir(src);
    struct dirent *e;

    if (d == NULL) {
        return;
    }
    mkdirs(dst);
    while ((e = readdir(d)) != NULL) {
        char s[1100], t[1100];
        struct stat st;
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        snprintf(s, sizeof(s), "%s/%s", src, e->d_name);
        snprintf(t, sizeof(t), "%s/%s", dst, e->d_name);
        if (stat(s, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            copy_tree(s, t);
        } else if (!exists(t)) {
            FILE *in = fopen(s, "rb"), *out = in ? fopen(t, "wb") : NULL;
            char buf[8192];
            size_t n;
            while (out != NULL && (n = fread(buf, 1, sizeof(buf), in)) > 0) {
                fwrite(buf, 1, n, out);
            }
            if (out) fclose(out);
            if (in) fclose(in);
        }
    }
    closedir(d);
}

void IosData_Translations(const char *gamedata) {
    char exe[1024], src[1100], dst[1100];
    uint32_t n = sizeof(exe);

    if (_NSGetExecutablePath(exe, &n) != 0) {
        return;
    }
    snprintf(src, sizeof(src), "%s/translations", dirname(exe));
    snprintf(dst, sizeof(dst), "%s/game-text/translations", gamedata);
    copy_tree(src, dst);
}

/* 1: the data are unpacked (the marker is written when the last file is) and the program of the game is there. */
int IosData_Ready(const char *gamedata) {
    char ready[700], slus[700];

    snprintf(ready, sizeof(ready), "%s/.bt3_ready", gamedata);
    snprintf(slus, sizeof(slus), "%s/disc/SLUS_216.78", gamedata);
    return exists(ready) && exists(slus);
}
