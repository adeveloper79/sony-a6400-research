/* Exact-version, reversible RAM-only dual-hook menu touch guard — ZV-E10 port (first pass, UNTESTED).
 *
 * Original: sony-a6400-research/tools/native_camera/native_menu_touch_full_guard.c (ILCE-6400).
 * ZV-E10 address model (blueprint docs/porting/zv-e10/zv-e10-touch-mod-blueprint.md §0/§1):
 *   - ZV-E10 §1 addresses are FILE OFFSETS / ELF VADRs, not runtime addresses.
 *     Runtime address = load base + offset; the load base is unknown until boot.
 *   - The a6400 guard baked ABSOLUTE runtime addresses into its constants; this port
 *     keeps the same shape with the blueprint values so the replacement points are
 *     explicit. A maps-derived rebase step (or -D overrides once the base is known)
 *     is REQUIRED before this can run. See zve10/README.md.
 *   - /proc/<pid>: a6400 used pid 149 ("im.elf"). ZV-E10 process name is LIKELY the
 *     same (im.elf) but the pid MUST be discovered at runtime — 149 is kept only as a
 *     placeholder default (-DMT_ZVE10_PID=<pid> once verified live).
 *
 * Hook targets: WidgetSystem::postMouseRelease / postMousePress IMPLEMENTATIONS (not the
 * PLT stubs) — see blueprint §1 "Hook candidates (dual-hook design)".
 *   postMouseRelease @ 0x00628488  (PLT 0x0011a128)
 *   postMousePress   @ 0x00628420  (PLT 0x001189b4)
 *
 * The 12-byte orig_rel/orig_prs prologue arrays below are the A6400'S BYTES and WILL NOT
 * match the ZV-E10. They are kept deliberately: every memcmp/readmem check then fails
 * closed and the guard refuses to install. Re-derive the ZV-E10 prologues from the live
 * libraries (or the firmware dump) before any deploy, then replace these arrays and the
 * branch-skip constants. HOOK_REL/HOOK_PRS + 12 (the rel/prs resume offset kept by the
 * trampoline) are likewise only valid for a 12-byte ZV-E10 prologue.
 */
#define _DEFAULT_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef MT_ZVE10_PID
#define MT_ZVE10_PID 149 /* ZV-E10: TO VERIFY — a6400 pid; discover at runtime (comm likely "im.elf") */
#endif
/* ZV-E10: injection-home candidate — libObj.so RX zero run, largest 16,254 B @ FILE OFFSET
 * 0x011964ee (~45 KB total across 15+ runs). viewUnified2.so alternative: 12,000 B @
 * 0x00a6ea30. CautionConfig.so has NO RX padding. CODE is a FILE OFFSET here, not a runtime
 * address — the installer must add the libObj load base (from /proc/<pid>/maps) before
 * ptrace/poke, or this must be -D overridden with the absolute address. */
#ifndef MT_ZVE10_CODE
#define MT_ZVE10_CODE 0x011964eeu /* ZV-E10: libObj.so RX zero home @ file offset (rebase required) — TO VERIFY */
#endif
#define CODE MT_ZVE10_CODE
/* ZV-E10: TO VERIFY — no ZV-E10 RW/RW-zero state home located yet (blueprint §4.2 wants a
 * loader-owned RW segment; §1 "RX zero padding" is code-only). a6400 value kept as
 * placeholder. Do NOT deploy until a ZV-E10 state home is verified. */
#ifndef MT_ZVE10_STATE
#define MT_ZVE10_STATE 0x7143920u /* ZV-E10: TO VERIFY — a6400 placeholder */
#endif
#define STATE MT_ZVE10_STATE
#define HOOK_REL 0x00628488u /* ZV-E10: WidgetSystem::postMouseRelease implementation (libObj file offset; PLT 0x0011a128) — TO VERIFY (rebase) */
#define HOOK_PRS 0x00628420u /* ZV-E10: WidgetSystem::postMousePress implementation (libObj file offset; PLT 0x001189b4) — TO VERIFY (rebase) */
#define CODE_CAP 2296u
#define STATE_CAP 1760u
#define STATE_SIZE 632u
#define REQ_MAGIC 0x4d544652u  /* "MTFR" */
#define STATE_MAGIC 0x4d544632u /* "MTF2" */
#define PROTO_VER 2u
#define EVENT_CAP 16u

/* ZV-E10: TO VERIFY — a6400 prologue bytes (postMouseRelease/postMousePress are DIFFERENT on
 * the ZV-E10; blueprint §1 "Hook prologue 12 bytes: Unknown — must be re-derived").
 * Left as placeholders so install fails closed until re-derived. */
static const unsigned char orig_rel[12] = {
    0x2d, 0xe9, 0xf0, 0x47, 0x88, 0xb0, 0x4f, 0xf0, 0x14, 0x08, 0x01, 0x22 /* ZV-E10: TO VERIFY (a6400 bytes) */
};
static const unsigned char orig_prs[12] = {
    0x2d, 0xe9, 0xf0, 0x47, 0x88, 0xb0, 0x4f, 0xf0, 0x14, 0x09, 0x01, 0x22 /* ZV-E10: TO VERIFY (a6400 bytes) */
};
static unsigned char payload[CODE_CAP], zeros[CODE_CAP], buffer[CODE_CAP];

static pid_t freeze_tids[512];
static unsigned freeze_count, freeze_attached;
static unsigned char freeze_owned[512], freeze_stopped[512];
static unsigned long long freeze_begin;

static unsigned long long freeze_ms(void) {
    struct { uint32_t sec; uint32_t nsec; } t = {0, 0};
    if (syscall(SYS_clock_gettime, 1, &t)) return 0;
    return (unsigned long long)t.sec * 1000ull + (unsigned long long)t.nsec / 1000000ull;
}

static int freeze_enumerate(pid_t *out, unsigned *count) {
    char task_path[64];
    snprintf(task_path, sizeof(task_path), "/proc/%d/task", MT_ZVE10_PID);
    DIR *dir = opendir(task_path);
    struct dirent *entry;
    unsigned n = 0;
    if (!dir) return -90;
    while ((entry = readdir(dir))) {
        char *end;
        long id = strtol(entry->d_name, &end, 10);
        if (!*entry->d_name || *end || id <= 0) continue;
        if (n >= 512) { closedir(dir); return -91; }
        out[n++] = (pid_t)id;
    }
    closedir(dir);
    *count = n;
    return 0;
}

static int freeze_resume(void) {
    int result = 0;
    unsigned resumed = 0;
    for (unsigned i = freeze_attached; i; i--) {
        unsigned n = i - 1;
        int status;
        if (!freeze_owned[n]) continue;
        if (!freeze_stopped[n]) {
            pid_t got = waitpid(freeze_tids[n], &status, __WALL | WNOHANG);
            if (got == freeze_tids[n] && WIFSTOPPED(status)) freeze_stopped[n] = 1;
            else if (got == freeze_tids[n] && (WIFEXITED(status) || WIFSIGNALED(status))) {
                freeze_owned[n] = 0;
                continue;
            } else { result = -92; continue; }
        }
        if (ptrace(PTRACE_DETACH, freeze_tids[n], 0, 0)) result = -92;
        else { freeze_owned[n] = freeze_stopped[n] = 0; resumed++; }
    }
    printf("FREEZE threads=%u attached=%u resumed=%u result=%d elapsed_ms=%llu\n",
           freeze_count, freeze_attached, resumed, result, freeze_ms() - freeze_begin);
    return result;
}

static int freeze_process(void) {
    pid_t check[512];
    unsigned confirmed = 0;
    int status, result;
    for (unsigned i = 0; i < freeze_attached; i++) if (freeze_owned[i]) return -100;
    freeze_count = freeze_attached = 0;
    freeze_begin = freeze_ms();
    if (!freeze_begin) return -93;
    result = freeze_enumerate(freeze_tids, &freeze_count);
    if (result || !freeze_count) return result ? result : -94;
    for (unsigned i = 0; i < freeze_count; i++) {
        if (ptrace(PTRACE_ATTACH, freeze_tids[i], 0, 0)) { result = -95; goto fail; }
        freeze_owned[i] = 1;
        freeze_stopped[i] = 0;
        freeze_attached++;
        for (;;) {
            pid_t got = waitpid(freeze_tids[i], &status, __WALL | WNOHANG);
            if (got == freeze_tids[i] && WIFSTOPPED(status)) { freeze_stopped[i] = 1; break; }
            if (got < 0 || freeze_ms() - freeze_begin > 2000) { result = -96; goto fail; }
            usleep(1000);
        }
    }
    result = freeze_enumerate(check, &confirmed);
    if (result || confirmed != freeze_count) { result = -97; goto fail; }
    for (unsigned i = 0; i < confirmed; i++) {
        unsigned j;
        for (j = 0; j < freeze_count; j++) if (check[i] == freeze_tids[j]) break;
        if (j == freeze_count) { result = -98; goto fail; }
    }
    return 0;
fail:
    if (freeze_resume()) return -99;
    return result;
}

static int readmem(int fd, uint32_t a, void *b, unsigned n) {
    return pread(fd, b, n, a) == (int)n ? 0 : -1;
}

static int poke(uint32_t a, const void *b, unsigned n) {
    if ((a | n) & 3u) return -1;
    for (unsigned i = 0; i < n; i += 4u) {
        uint32_t word;
        memcpy(&word, (const unsigned char *)b + i, 4);
        if (ptrace(PTRACE_POKETEXT, MT_ZVE10_PID, (void *)(uintptr_t)(a + i), (void *)(uintptr_t)word)) {
            return -1;
        }
    }
    return 0;
}

static int maps_ok(void) {
    char maps_path[64];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", MT_ZVE10_PID);
    /* ZV-E10: TO VERIFY — process layout expected to be im.elf + /libObj.so + /viewUnified2.so
     * + /CautionConfig.so (blueprint §1 row "/proc/<pid>/maps line names ... verify live").
     * The r-xp//libObj.so identity check below is the minimum; a hardened installer must also
     * verify .so identity (build-id / hash) and derive bases for ALL three libraries before
     * rebasing CODE/STATE/HOOK_* (see zve10/README.md). */
    FILE *f = fopen(maps_path, "r");
    if (!f) return 0;
    char line[256], perms[5];
    unsigned lo, hi;
    int code = 0, state = 0;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "%x-%x %4s", &lo, &hi, perms) == 3) {
            if (lo <= CODE && CODE + CODE_CAP <= hi && !strcmp(perms, "r-xp") && strstr(line, "/libObj.so")) code = 1;
            if (lo <= STATE && STATE + STATE_CAP <= hi && !strcmp(perms, "rw-p")) state = 1;
        }
    }
    fclose(f);
    return code && state;
}

static void make_branch(unsigned char out[12], uint32_t target) {
    unsigned char tmpl[12] = {0xdf, 0xf8, 0, 0xf0, 0, 0, 0, 0, 0, 0xbf, 0, 0xbf};
    memcpy(tmpl + 4, &target, 4);
    memcpy(out, tmpl, 12);
}

int main(void) {
    /* h: magic, ver, op, code_bytes, rel_entry, prs_entry, fnv1a, state_size, hook_rel, hook_prs */
    uint32_t h[10];
    /* ZV-E10: TO VERIFY — /tmp_bt existence on the ZV-E10 is UNVERIFIED (blueprint §1). */
    FILE *f = fopen("/tmp_bt/menu_touch_full.req", "rb");
    if (!f) return 2;
    if (fread(h, 1, sizeof(h), f) != sizeof(h) || h[0] != REQ_MAGIC || h[1] != PROTO_VER ||
        h[2] > 2u || !h[3] || h[3] > sizeof(payload) || (h[3] % 4u) ||
        !(h[4] & 1u) || (h[4] & ~1u) < CODE || (h[4] & ~1u) >= CODE + h[3] ||
        !(h[5] & 1u) || (h[5] & ~1u) < CODE || (h[5] & ~1u) >= CODE + h[3] ||
        h[7] != STATE_SIZE || h[8] != HOOK_REL || h[9] != HOOK_PRS ||
        fread(payload, 1, h[3], f) != h[3] || fgetc(f) != EOF) {
        fclose(f);
        return 3;
    }
    fclose(f);

    uint32_t sum = 2166136261u;
    for (unsigned i = 0; i < h[3]; i++) sum = (sum ^ payload[i]) * 16777619u;
    if (sum != h[6]) return 4;

    /* ZV-E10: TO VERIFY — process name is LIKELY "im.elf" (same app framework; blueprint §1
     * row "/proc/<pid>/comm = im.elf ... verify live"). */
    char comm[32] = {0};
    char comm_path[64];
    snprintf(comm_path, sizeof(comm_path), "/proc/%d/comm", MT_ZVE10_PID);
    f = fopen(comm_path, "r");
    if (!f) return 5;
    int identity = fgets(comm, sizeof(comm), f) && !strcmp(comm, "im.elf\n");
    fclose(f);
    if (!identity) return 5;

    unsigned char br_rel[12], br_prs[12];
    make_branch(br_rel, h[4]);
    make_branch(br_prs, h[5]);

    int result = freeze_process(), fd = -1, changed = 0;
    if (result) goto end;
    {
        char mem_path[64];
        snprintf(mem_path, sizeof(mem_path), "/proc/%d/mem", MT_ZVE10_PID);
        fd = open(mem_path, h[2] ? O_RDWR : O_RDONLY);
    }
    if (fd < 0) { result = -10; goto done; }
    if (!maps_ok()) { result = -11; goto done; }

    if (readmem(fd, HOOK_REL, buffer, 12) || memcmp(buffer, h[2] == 2u ? br_rel : orig_rel, 12) ||
        readmem(fd, HOOK_PRS, buffer, 12) || memcmp(buffer, h[2] == 2u ? br_prs : orig_prs, 12)) {
        result = -12;
        goto done;
    }
    for (unsigned i = 0; i < freeze_count; i++) {
        uint32_t regs[18];
        if (ptrace(PTRACE_GETREGS, freeze_tids[i], 0, regs)) { result = -13; goto done; }
        uint32_t pc = regs[15] & ~1u;
        if ((HOOK_REL <= pc && pc < HOOK_REL + 12u) ||
            (HOOK_PRS <= pc && pc < HOOK_PRS + 12u) ||
            (CODE <= pc && pc < CODE + h[3])) {
            result = -14;
            goto done;
        }
    }

    if (h[2] == 2u) {
        uint32_t state[6];
        if (readmem(fd, STATE, state, sizeof(state)) || state[0] != STATE_MAGIC ||
            state[1] != PROTO_VER || state[5] != EVENT_CAP) {
            result = -15;
            goto done;
        }
        uint32_t disabled = 0;
        if (pwrite(fd, &disabled, 4, STATE + 8) != 4) { result = -16; goto done; }
        if (state[4]) { result = -17; goto done; }
        if (readmem(fd, CODE, buffer, h[3]) || memcmp(buffer, payload, h[3])) { result = -18; goto done; }
        if (poke(HOOK_REL, orig_rel, 12) || poke(HOOK_PRS, orig_prs, 12) ||
            poke(CODE, zeros, h[3]) || pwrite(fd, zeros, STATE_SIZE, STATE) != STATE_SIZE) {
            result = -19;
            goto done;
        }
        if (readmem(fd, HOOK_REL, buffer, 12) || memcmp(buffer, orig_rel, 12) ||
            readmem(fd, HOOK_PRS, buffer, 12) || memcmp(buffer, orig_prs, 12) ||
            readmem(fd, CODE, buffer, h[3]) || memcmp(buffer, zeros, h[3]) ||
            readmem(fd, STATE, buffer, STATE_SIZE) || memcmp(buffer, zeros, STATE_SIZE)) {
            result = -20;
            goto done;
        }
        printf("REMOVED hooks=00628488,00628420 code_restored=1 state_restored=1\n");
    } else {
        if (readmem(fd, CODE, buffer, CODE_CAP) || memcmp(buffer, zeros, CODE_CAP) ||
            readmem(fd, STATE, buffer, STATE_CAP) || memcmp(buffer, zeros, STATE_CAP)) {
            result = -21;
            goto done;
        }
        printf("CHECK op=%u rel=%08x prs=%08x code_bytes=%u owned_zero_padding=1 pc_clear=1\n",
               h[2], h[4], h[5], h[3]);
        if (h[2] == 1u) {
            uint32_t state[6] = {STATE_MAGIC, PROTO_VER, 2u, 0u, 0u, EVENT_CAP};
            changed = 1;
            if (pwrite(fd, state, sizeof(state), STATE) != sizeof(state) ||
                poke(CODE, payload, h[3]) || poke(HOOK_REL, br_rel, 12) ||
                poke(HOOK_PRS, br_prs, 12)) {
                result = -22;
                goto rollback;
            }
            if (readmem(fd, HOOK_REL, buffer, 12) || memcmp(buffer, br_rel, 12) ||
                readmem(fd, HOOK_PRS, buffer, 12) || memcmp(buffer, br_prs, 12) ||
                readmem(fd, CODE, buffer, h[3]) || memcmp(buffer, payload, h[3]) ||
                readmem(fd, STATE, buffer, sizeof(state)) || memcmp(buffer, state, sizeof(state))) {
                result = -23;
                goto rollback;
            }
            printf("INSTALLED hooks=00628488,00628420 rel=%08x prs=%08x code_bytes=%u state_bytes=%u\n",
                   h[4], h[5], h[3], STATE_SIZE);
        }
    }
    goto done;

rollback:
    if (changed) {
        int ok = !poke(HOOK_REL, orig_rel, 12) && !poke(HOOK_PRS, orig_prs, 12) &&
                 !poke(CODE, zeros, h[3]) && pwrite(fd, zeros, STATE_SIZE, STATE) == STATE_SIZE;
        printf("ROLLBACK result=%d\n", ok ? 0 : -1);
        if (!ok) result = -99;
    }

done:
    if (fd >= 0) close(fd);
    if (freeze_resume()) result = -99;

end:
    printf("END result=%d op=%u errno=%d\n", result, h[2], errno);
    fflush(stdout);
    return result ? 6 : 0;
}
