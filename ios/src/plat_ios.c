/* iOS: the process's start, SDL's UIKit application, and the game's run from a mirror of the program in a window of
   its own.

   The game's addresses are offsets in a 4 GB window whose base has its low 32 bits zero (port_addr.h, macwin.cpp).
   On macOS the program is linked at 1 TB and starts itself again with ASLR switched off, to be there. An iOS app can
   do neither (posix_spawn is refused, and the process has one free stretch of address space of about 6 GB, which
   starts on a 4 GB boundary). So the window is reserved here, aligned, and the program is MIRRORED into it at
   W + 0x20000000: its code and data are mapped a second time at that address (vm_remap, the same pages, still signed),
   and the game runs from the mirror, where every address the compiled code computes from its own position is a game
   address. The pointers the program holds in its data (rebase entries) are moved to the mirror, as a loader does.

   Everything is logged to Documents/bt3_log.txt (visible in the Files app): there is no console on the phone. */
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define say(...) fprintf(stderr, __VA_ARGS__)

/* SDL's own entry for an application (SDL_main.h): UIKit runs, and `main_fn` is called on the main thread once the
   application has launched. Declared here: the port's files are compiled without SDL's include path. */
typedef int (*main_fn)(int argc, char **argv);
extern int SDL_RunApp(int argc, char **argv, main_fn mainFunction, void *reserved);

/* port/src/gs/ios_setup.c: unpacks the user's disc image into <Documents>/gamedata, with a progress screen */
extern int Ios_Setup(void);
/* port/src/gs/ios_touch.c: the on-screen controller (Objective-C), put on the game's window once there is one */
extern void Ios_TouchStart(void);

extern void *Port_Window;
extern void Port_MapMemory(void);
extern int Port_GameEntry(int argc, char **argv);

typedef struct {
    uint64_t vm, size, fileoff;
    char name[17];
} Seg;

static Seg sSeg[16];
static int sNSeg;
static uint64_t sObjc[32][2]; /* sections of Objective-C metadata: left as they are (the runtime registered them where they were) */
static int sNObjc;
static uint64_t sOrigLo, sAlias0;
static uint64_t sWOrig, sImgEnd;       /* the window as the system placed it, and where the program ends */
static uint64_t sReloc, sSkipped, sOutside;

static void fail(const char *what) {
    say("bt3: %s\n", what);
    exit(3);
}

/* The engine's settings file (bt3_settings.txt, `name=number` lines, in the current directory; for a name that comes
   twice the last line counts). The iOS defaults are put there once: the first time the file has no `ios_defaults`
   line they are added to the end (which wins over a value the engine saved before it knew them), and from then on the
   player's own choices in the settings window are left alone. */
static const char *settings_path(void) {
    const char *p = getenv("BT3_SETTINGS");
    return p != NULL && p[0] != 0 ? p : "bt3_settings.txt";
}

static int has_setting(const char *name) {
    char line[160];
    size_t n = strlen(name);
    int found = 0;
    FILE *f = fopen(settings_path(), "r");

    while (f != NULL && fgets(line, sizeof(line), f) != NULL) {
        if (strncmp(line, name, n) == 0 && line[n] == '=') {
            found = 1;
        }
    }
    if (f != NULL) {
        fclose(f);
    }
    return found;
}

static void seed_defaults(void) {
    FILE *f;

    if (has_setting("ios_defaults")) {
        return;
    }
    if ((f = fopen(settings_path(), "a")) != NULL) {
        fputs("\nlate_input=1\nios_defaults=1\n", f);
        fclose(f);
    }
}

/* The folder of the memory card: made here, and a file written and removed in it, so the log says whether the app's
   folder takes saves at all (the game says only "cannot save"). */
static void check_saves(void) {
    const char *root = getenv("BT3_SAVES");
    char dir[1100], test[1200];
    FILE *f;

    if (root == NULL) {
        return;
    }
    snprintf(dir, sizeof(dir), "%s/card1", root);
    mkdir(root, 0755);
    mkdir(dir, 0755);
    snprintf(test, sizeof(test), "%s/.write_test", dir);
    if ((f = fopen(test, "wb")) != NULL) {
        fputs("ok", f);
        fclose(f);
        remove(test);
        say("bt3: saves folder %s: can be written\n", dir);
    } else {
        say("bt3: saves folder %s: CANNOT be written (errno %d: %s)\n", dir, errno, strerror(errno));
    }
}

/* ---- log: stderr and stdout go to Documents/bt3_log.txt (the run before kept as bt3_log_prev.txt) */
static void log_setup(void) {
    const char *home = getenv("HOME");
    char path[1024], prev[1024], data[1024];

    if (home == NULL) {
        return;
    }
    snprintf(path, sizeof(path), "%s/Documents/bt3_log.txt", home);
    snprintf(prev, sizeof(prev), "%s/Documents/bt3_log_prev.txt", home);
    rename(path, prev);
    if (freopen(path, "w", stderr) != NULL) {
        setvbuf(stderr, NULL, _IONBF, 0);
        dup2(fileno(stderr), STDOUT_FILENO);
    }
    snprintf(data, sizeof(data), "%s/Documents/gamedata", home);
    setenv("BT3_DATA", data, 0);
    snprintf(data, sizeof(data), "%s/Documents", home);
    if (chdir(data) != 0) {
        say("bt3: cannot enter %s\n", data);
    }
    /* "Low input lag" (F1 > Video in the engine's settings window): the wait for a picture is made just before the pads
       are read, and one picture at most is on its way to the screen. Off in the engine; on here, where the buttons are
       on glass: a press then waits a picture less to reach the game. Turned off in the settings window, it stays off. */
    /* where the engine keeps what is the player's, by absolute path: not the current directory's business */
    snprintf(data, sizeof(data), "%s/Documents/saves", home);
    setenv("BT3_SAVES", data, 0);
    snprintf(data, sizeof(data), "%s/Documents/bt3_settings.txt", home);
    setenv("BT3_SETTINGS", data, 0);
    /* SDL's audio session on iOS: "ambient" mixes with what the phone is already playing (music, a podcast) and
       follows the silent switch; SDL's default, "playback", takes the sound over. (An SDL hint, read from the
       environment too.) */
    setenv("SDL_AUDIO_CATEGORY", "ambient", 0);
    seed_defaults();
    check_saves();
}

/* ---- the program's own load commands */
static const struct dyld_info_command *parse_image(const struct mach_header_64 *h, intptr_t slide) {
    const struct dyld_info_command *di = NULL;
    const uint8_t *p = (const uint8_t *)(h + 1);
    uint32_t i, k;

    sNSeg = 0;
    sNObjc = 0;
    for (i = 0; i < h->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)p;
        if (lc->cmd == LC_SEGMENT_64 && sNSeg < 16) {
            const struct segment_command_64 *sc = (const struct segment_command_64 *)lc;
            const struct section_64 *sec = (const struct section_64 *)(sc + 1);
            Seg *s = &sSeg[sNSeg++];
            s->vm = sc->vmaddr + (uint64_t)slide;
            s->size = sc->vmsize;
            s->fileoff = sc->fileoff;
            memcpy(s->name, sc->segname, 16);
            s->name[16] = 0;
            for (k = 0; k < sc->nsects; k++, sec++) {
                if (strncmp(sec->sectname, "__objc", 6) == 0 && sNObjc < 32) {
                    sObjc[sNObjc][0] = sec->addr + (uint64_t)slide;
                    sObjc[sNObjc][1] = sec->addr + (uint64_t)slide + sec->size;
                    sNObjc++;
                }
            }
        } else if (lc->cmd == LC_DYLD_INFO || lc->cmd == LC_DYLD_INFO_ONLY) {
            di = (const struct dyld_info_command *)lc;
        }
        p += lc->cmdsize;
    }
    return di;
}

/* ---- the window: 4 GB aligned, in the process's one free stretch */
static uint64_t reserve_window(void) {
    static const uint64_t gb[] = {6, 5, 4};
    unsigned i;

    for (i = 0; i < sizeof(gb) / sizeof(gb[0]); i++) {
        vm_address_t base = 0;
        vm_size_t total = (vm_size_t)(gb[i] << 30);
        kern_return_t kr = vm_map(mach_task_self(), &base, total, 0, VM_FLAGS_ANYWHERE, MEMORY_OBJECT_NULL, 0, FALSE, VM_PROT_NONE,
                                  VM_PROT_READ | VM_PROT_WRITE, VM_INHERIT_DEFAULT);
        uint64_t w;

        if (kr != KERN_SUCCESS) {
            say("bt3: reserving %llu GB failed (%d)\n", gb[i], (int)kr);
            continue;
        }
        w = ((uint64_t)base + 0xFFFFFFFFull) & ~0xFFFFFFFFull;
        say("bt3: reserved %llu GB at 0x%llx, window at 0x%llx, %llu MB after it\n", gb[i], (uint64_t)base, w, ((uint64_t)base + total - w) >> 20);
        if ((uint64_t)base + total - w >= 0x80000000ull) {
            return w;
        }
        vm_deallocate(mach_task_self(), base, total);
    }
    fail("no aligned window of 2 GB could be reserved");
    return 0;
}

/* ---- the mirror: the program's segments (not the zero page, not the link edit data) mapped again at W + 0x20000000 */
static void mirror_segments(uint64_t w) {
    int i;

    sAlias0 = w + 0x20000000ull;
    for (i = 0; i < sNSeg; i++) {
        vm_address_t dst;
        vm_prot_t cur = 0, mx = 0;
        kern_return_t kr;

        if (strcmp(sSeg[i].name, "__PAGEZERO") == 0 || strcmp(sSeg[i].name, "__LINKEDIT") == 0) {
            continue;
        }
        dst = (vm_address_t)(sAlias0 + (sSeg[i].vm - sOrigLo));
        kr = vm_remap(mach_task_self(), &dst, (vm_size_t)sSeg[i].size, 0, VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE, mach_task_self(),
                      (vm_address_t)sSeg[i].vm, FALSE, &cur, &mx, VM_INHERIT_NONE);
        say("bt3: mirror of %s at 0x%llx: %s (kern %d, protection %d/%d)\n", sSeg[i].name, (uint64_t)dst, kr == KERN_SUCCESS ? "ok" : "FAILED", (int)kr,
            (int)cur, (int)mx);
        if (kr != KERN_SUCCESS) {
            fail("the program could not be mirrored");
        }
    }
}

/* ---- the program's pointers in its data: moved to the mirror (the rebase opcodes dyld applied its slide with) */
static uint64_t uleb(const uint8_t **pp) {
    uint64_t r = 0;
    int shift = 0;
    uint8_t b;

    do {
        b = *(*pp)++;
        r |= (uint64_t)(b & 0x7F) << shift;
        shift += 7;
    } while (b & 0x80);
    return r;
}

/* Only a pointer into the program, or into the window below it (constants made from the window's base), is moved.
   A location the rebase information lists may hold something else by now: dyld bound the lazy symbol pointers to
   their functions after it rebased them, and the program has been running (SDL, UIKit) since. Moving those would
   break them: the first version did, and the first call of a C library function after it crashed. */
static void rebase_at(uint64_t addr, uint64_t delta) {
    uint64_t v;
    int i;

    for (i = 0; i < sNObjc; i++) {
        if (addr >= sObjc[i][0] && addr < sObjc[i][1]) {
            sSkipped++;
            return;
        }
    }
    v = *(uint64_t *)(uintptr_t)addr;
    if (v < sWOrig || v >= sImgEnd) {
        sOutside++;
        return;
    }
    *(uint64_t *)(uintptr_t)addr = v + delta;
    sReloc++;
}

static void relocate(const struct dyld_info_command *di, uint64_t delta) {
    uint64_t le = 0, leOff = 0, addr = 0, n, skip;
    const uint8_t *p, *end;
    int i, type = 1;

    for (i = 0; i < sNSeg; i++) {
        if (strcmp(sSeg[i].name, "__LINKEDIT") == 0) {
            le = sSeg[i].vm;
            leOff = sSeg[i].fileoff;
        }
    }
    if (di == NULL || le == 0 || di->rebase_size == 0) {
        fail("the program has no rebase information (it must be linked without chained fixups)");
    }
    sWOrig = sOrigLo - 0x20000000ull;
    sImgEnd = 0;
    for (i = 0; i < sNSeg; i++) {
        if (strcmp(sSeg[i].name, "__PAGEZERO") != 0 && strcmp(sSeg[i].name, "__LINKEDIT") != 0 && sSeg[i].vm + sSeg[i].size > sImgEnd) {
            sImgEnd = sSeg[i].vm + sSeg[i].size;
        }
    }
    say("bt3: relocating: rebase information %u bytes, moving pointers in [0x%llx, 0x%llx) by 0x%llx\n", (unsigned)di->rebase_size, sWOrig, sImgEnd, delta);
    /* the data segments are writable for this, then the constant ones are closed again */
    for (i = 0; i < sNSeg; i++) {
        if (strncmp(sSeg[i].name, "__DATA", 6) == 0 && mprotect((void *)(uintptr_t)sSeg[i].vm, (size_t)sSeg[i].size, PROT_READ | PROT_WRITE) != 0) {
            fail("cannot make the data segment writable");
        }
    }
    p = (const uint8_t *)(uintptr_t)(le + (di->rebase_off - leOff));
    end = p + di->rebase_size;
    while (p < end) {
        uint8_t op = *p++, imm = op & REBASE_IMMEDIATE_MASK;
        op &= REBASE_OPCODE_MASK;
        if (op == REBASE_OPCODE_DONE) {
            break;
        } else if (op == REBASE_OPCODE_SET_TYPE_IMM) {
            type = imm;
        } else if (op == REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB) {
            if (imm >= sNSeg) {
                fail("bad segment in the rebase information");
            }
            addr = sSeg[imm].vm + uleb(&p);
        } else if (op == REBASE_OPCODE_ADD_ADDR_ULEB) {
            addr += uleb(&p);
        } else if (op == REBASE_OPCODE_ADD_ADDR_IMM_SCALED) {
            addr += (uint64_t)imm * 8;
        } else if (op == REBASE_OPCODE_DO_REBASE_IMM_TIMES) {
            for (n = 0; n < imm; n++, addr += 8) {
                if (type == REBASE_TYPE_POINTER) rebase_at(addr, delta);
            }
        } else if (op == REBASE_OPCODE_DO_REBASE_ULEB_TIMES) {
            uint64_t count = uleb(&p);
            for (n = 0; n < count; n++, addr += 8) {
                if (type == REBASE_TYPE_POINTER) rebase_at(addr, delta);
            }
        } else if (op == REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB) {
            if (type == REBASE_TYPE_POINTER) rebase_at(addr, delta);
            addr += uleb(&p) + 8;
        } else if (op == REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_ULEB) {
            uint64_t count = uleb(&p);
            skip = uleb(&p);
            for (n = 0; n < count; n++, addr += skip + 8) {
                if (type == REBASE_TYPE_POINTER) rebase_at(addr, delta);
            }
        } else {
            fail("unknown rebase opcode");
        }
    }
    for (i = 0; i < sNSeg; i++) {
        if (strcmp(sSeg[i].name, "__DATA_CONST") == 0) {
            mprotect((void *)(uintptr_t)sSeg[i].vm, (size_t)sSeg[i].size, PROT_READ);
        }
    }
    say("bt3: %llu pointers moved to the mirror, %llu in Objective-C metadata left, %llu pointing elsewhere left\n", sReloc, sSkipped, sOutside);
}

/* What runs from the mirror: the memory of the game (what the constructor did on macOS), then the game. */
__attribute__((noinline)) static int game_via_mirror(int argc, char **argv) {
    Ios_TouchStart();
    Port_MapMemory();
    return Port_GameEntry(argc, argv);
}

static int app_main(int argc, char **argv) {
    const struct mach_header_64 *h = (const struct mach_header_64 *)_dyld_get_image_header(0);
    intptr_t slide = _dyld_get_image_vmaddr_slide(0);
    const struct dyld_info_command *di = parse_image(h, slide);
    uint64_t w;
    int (*fn)(int, char **);

    sOrigLo = (uint64_t)(uintptr_t)h;
    say("bt3: iOS start; the program is at 0x%llx (slide 0x%lx)\n", sOrigLo, (long)slide);
    if (!Ios_Setup()) {
        fail("the game's data could not be prepared");
    }
    w = reserve_window();
    mirror_segments(w);
    relocate(di, sAlias0 - sOrigLo);
    Port_Window = (void *)(uintptr_t)w;
    fn = (int (*)(int, char **))(uintptr_t)(sAlias0 + ((uint64_t)(uintptr_t)game_via_mirror - sOrigLo));
    say("bt3: entering the game through the mirror (window 0x%llx, game_via_mirror at %p)\n", w, (void *)fn);
    return fn(argc, argv);
}

/* The process's entry (link.py: -e ___wrap_main): dyld calls it as main. SDL runs UIKit and calls app_main on the main
   thread once the application has launched. */
int __wrap_main(int argc, char **argv) {
    log_setup();
    say("bt3: starting\n");
    return SDL_RunApp(argc, argv, app_main, NULL);
}
#endif
