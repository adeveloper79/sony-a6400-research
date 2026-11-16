/* Guarded full-menu touch prototype — ZV-E10 port (first pass, UNTESTED):
 * - Main menu (PAS_MenuListTab): tap tabs, page dots, rows (1st=select, 2nd=enter),
 *   plus horizontal swipe left/right for next/prev page.
 * - Popup menu (PAS_MenuListPopup): tap row to apply+close, vertical swipe to scroll,
 *   tap outside popup panel to cancel/close.
 *
 * ZV-E10 (BIONZ X / CXD90045, CE Linux 8.2.7.6, FW 2.03; sibling of the ILCE-6400).
 * Every address from the ILCE-6400 build was re-derived from
 * docs/porting/zv-e10/zv-e10-touch-mod-blueprint.md (2026-11-16, offline RE).
 * NO HARDWARE HAS BEEN TOUCHED. This file is a structural first pass only.
 *
 * ADDRESS MODEL (blueprint §0): ZV-E10 addresses in that document are FILE OFFSETS /
 * ELF VADRs of the ZV-E10 libraries, NOT runtime addresses. `.text`: file == vaddr.
 * Second-segment objects (.data.rel.ro/.got/.data/.bss): vaddr = file + 0x8000 (for
 * CautionConfig.so the delta is 0x7000, e.g. CmnViewSettingNode vtable file 0xfa3968
 * -> VA 0xfab968). Runtime address = load base + offset, and the load base is unknown
 * until boot. The code keeps the a6400's "compile-time absolute constant" shape so that
 * every replacement point is visible; an installer must rebase these constants against
 * /proc/<pid>/maps (or the payload must be rebuilt with maps-derived bases) before it
 * can run. Struct offsets are load-base independent. See zve10/README.md.
 *
 * Injection-home candidate (blueprint §1 "RX zero padding"): libObj.so, largest owned
 * RX zero run 16,254 B @ 0x011964ee (file offset). viewUnified2.so alternative:
 * 12,000 B @ 0x00a6ea30. CautionConfig.so has NO usable RX padding. Whether any zero
 * run is provably dead cannot be verified offline.
 * State (RW) home: NO ZV-E10 RW zero region has been located. MT_STATE_ADDR below
 * keeps the a6400 value with a TO VERIFY marker — do NOT deploy as-is.
 */
#include <stdint.h>

#define MT_FULL_MAGIC 0x4d544632u /* "MTF2" */
#define MT_FULL_VERSION 2u
/* ZV-E10: WidgetSystem singleton — .bss VA 0x0145cb1c (unexported; GOT slot 0x013df230
 * via sys_getInstance @ 0x00616fe4). Was a6400 0x956f0. Needs libObj load base. */
#define MT_SYSTEM_ADDR 0x0145cb1cu
/* ZV-E10: TO VERIFY — no ZV-E10 RW/RW-zero state home identified yet (blueprint §1
 * "RX zero padding" covers code only; §4.2 requires a loader-owned RW segment).
 * a6400 value kept as placeholder. Do NOT deploy. */
#define MT_STATE_ADDR 0x7143920u /* ZV-E10: TO VERIFY */
#define MT_EVENT_CAP 16u

#define MT_ACT_NONE 0u
#define MT_ACT_TAB 1u
#define MT_ACT_PAGE 2u
#define MT_ACT_ROW_SELECT 3u
#define MT_ACT_ROW_ENTER 4u
#define MT_ACT_SWIPE_PAGE 5u
#define MT_ACT_POPUP_SELECT 6u
#define MT_ACT_POPUP_SCROLL 7u
#define MT_ACT_POPUP_CANCEL 8u

#ifndef MT_MAX_TABS
#define MT_MAX_TABS 6u /* Mod Loader build: -DMT_MAX_TABS=7u (injected Mods tab) */
#endif
#define MT_MAX_PAGES 15u
#define MT_MAX_ROWS 6u

/* ZV-E10: hit-test geometry/strides UNVERIFIED. Blueprint §2.6 / §1: tab-bar focus byte
 * (menu+0x1d8), main-state byte (menu+0x21), inherited-mode gate, tab strip menu+0x0b9c
 * (stride 0x220), page dots menu+0x166b4 (stride 0x220), rows menu+0x9350 (stride 0x1118),
 * popup panel regions (menu+0x1070 stride 0x398, menu+0x36a8 stride 0x220, menu+0x64c,
 * menu+0x60d8, menu+0x5c04, menu+0x2780) all depend on initialized live geometry and
 * "may match, may not" — verify against a live menu session. Struct LAYOUT itself is
 * confirmed identical (menu+0x186ac root-tree pointer, byte-identical semantics). */

struct mt_point {
    float x;
    float y;
};

struct mt_full_event {
    uint32_t seq;
    uint32_t tid;
    uint32_t action;
    uint32_t target;
    int32_t x;
    int32_t y;
    uint32_t tab;
    uint32_t page;
    uint32_t row;
};

struct mt_full_state {
    uint32_t magic;
    uint32_t version;
    uint32_t enabled;
    uint32_t count;
    uint32_t inflight;
    uint32_t capacity;
    uint32_t last_action;
    uint32_t last_target;
    int32_t last_x;
    int32_t last_y;
    uint32_t last_err;
    int32_t press_x;
    int32_t press_y;
    uint32_t press_valid;
    struct mt_full_event events[MT_EVENT_CAP];
};

extern uint32_t mt_original_release(void *, void *) __attribute__((visibility("hidden")));
extern uint32_t mt_original_press(void *, void *) __attribute__((visibility("hidden")));

static uint32_t rd32(uint32_t addr, unsigned off) {
    return *(const volatile uint32_t *)(addr + off);
}

static unsigned rd8(uint32_t addr, unsigned off) {
    return *(const volatile uint8_t *)(addr + off);
}

static uint32_t get_tid(void) {
    register uint32_t r0 __asm__("r0") = 0;
    register uint32_t r7 __asm__("r7") = 224;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r7) : "memory", "cc");
    return r0;
}

static int iabs(int v) {
    return v < 0 ? -v : v;
}

static void record_event(volatile struct mt_full_state *s, uint32_t action, uint32_t target,
                         int32_t x, int32_t y, uint32_t tab, uint32_t page, uint32_t row) {
    uint32_t idx = __sync_fetch_and_add(&s->count, 1u);
    s->last_action = action;
    s->last_target = target;
    s->last_x = x;
    s->last_y = y;
    if (idx < MT_EVENT_CAP) {
        volatile struct mt_full_event *e = &s->events[idx];
        e->tid = get_tid();
        e->action = action;
        e->target = target;
        e->x = x;
        e->y = y;
        e->tab = tab;
        e->page = page;
        e->row = row;
        __sync_synchronize();
        e->seq = idx + 1u;
    }
}

static void sync_refresh(uint32_t menu) {
    /* ZV-E10: TO VERIFY — the a6400 "binding" (0x15b91431) has NO equivalent in the blueprint
     * (blueprint §1 row "Widget hit-test / binding / refresh ...": binding TBD).
     * Refresh candidates named by the blueprint: PAS vtable idx108 @ 0x8629f8
     * (+0x1b0, dirty-flag clear + refresh) and 0x2f5998 (post-move refresh).
     * Identity and (menu, r1=0) signature NOT verified — keep a6400 constants, fail loudly live. */
    void (*binding)(uint32_t) = (void *)0x15b91431u; /* ZV-E10: TO VERIFY */
    void (*refresh)(uint32_t, uint32_t) = (void *)0x15b92111u; /* ZV-E10: TO VERIFY (candidates: 0x8629f8 idx108, 0x2f5998 post-move refresh) */
    binding(menu);
    refresh(menu, 0u);
}

static uint32_t find_setting_menu_view(uint32_t menu) {
    uint32_t vid = rd32(menu, 0x14cu);
    /* ZV-E10: TO VERIFY — a6400 ViewSettingMenu-instance list holder @ 0x0713D7EC;
     * not located/mapped in the blueprint. */
    uint32_t vm = rd32(0x0713D7ECu, 0u); /* ZV-E10: TO VERIFY */
    if (!vm) {
        return 0u;
    }
    int cnt = (int)rd32(vm, 0u);
    uint32_t entries = rd32(vm, 0x20u);
    if (!entries || cnt <= 0 || cnt > 128) {
        return 0u;
    }
    for (int i = 0; i < cnt; i++) {
        uint32_t ent = entries + (uint32_t)i * 16u;
        uint32_t vwrap = rd32(ent, 4u);
        if (!vwrap || rd32(ent, 8u) != vid) {
            continue;
        }
        /* ZV-E10: ViewSettingMenu vtable address point — _ZTV @ 0xbaaba8, address point
         * 0xbaabb0 (RTTI 0xbaada4; typeinfo name 0x8ef14f). Was a6400 0x15EC8270. */
        if (rd32(vwrap, 0u) == 0xbaabb0u) {
            return vwrap;
        }
        uint32_t inner = rd32(vwrap, 0x18u);
        if (inner && rd32(inner, 0u) == 0xbaabb0u) {
            return inner;
        }
    }
    return 0u;
}

static void push_async_event(uint32_t event_id, uint8_t ev_type) {
    /* ZV-E10: EventManager singleton holder — g_pEvtMgr, .bss VA 0x014920e0
     * (STT_OBJECT, exported; GOT slot 0x013ded14; deref helpers 0x0014cff8/0x0014d07c/0x0012c62c).
     * Was a6400 0x07089984. Deref DEPTH unverified — the a6400 used a double dereference and
     * it is kept as-is here; verify against live disassembly (g_pEvtMgr is STT_OBJECT,
     * likely a single-level EventManager* or a pointer-to-holder depending on the build). */
    uint32_t mgr = rd32(rd32(0x014920e0u, 0u), 0u); /* ZV-E10: TO VERIFY (deref depth; g_pEvtMgr) */
    if (!mgr || !event_id) {
        return;
    }
    /* ZV-E10: _Znwj is an IMPORTED symbol (not locally defined). PLT stub @ libObj file offset
     * 0x00108e0c (GOT 0x013d80a8). Calling it requires the PLT stub resolved at the libObj load
     * base, or a GOT read. Was a6400 0x05E15974. */
    void *(*op_new)(uint32_t) = (void *)0x00108e0cu; /* ZV-E10: TO VERIFY — _Znwj PLT stub (libObj file offset) */
    /* ZV-E10: Event::Event(unsigned long, unsigned char, unsigned char) @ 0x008a01e8
     * (copy ctor @ 0x008a021f; setParamList @ 0x008a0269). Was a6400 0x05E14E20. */
    void (*ev_ctor)(void *, uint32_t, uint8_t, uint8_t) = (void *)0x008a01e8u;
    /* ZV-E10: EventManager::push @ 0x008a1794 (PLT 0x0010c2a8). Was a6400 0x05E18D5D. */
    void (*evmgr_push)(uint32_t, void *, uint32_t) = (void *)0x008a1794u;
    void *ev = op_new(16u);
    if (!ev) {
        return;
    }
    ev_ctor(ev, event_id, ev_type, 1u);
    evmgr_push(mgr, ev, 1u);
}

static void handle_popup_touch(volatile struct mt_full_state *s, uint32_t menu,
                               int16_t x, int16_t y, int dx, int dy, int has_press) {
    uint32_t popup = menu + 0xfb20u;
    uint32_t pnode = rd32(popup, 0x6490u);
    /* ZV-E10: CmnViewSettingNode vtable — VA 0x00fab968 (CautionConfig.so; file 0xfa3968;
     * RTTI _ZTI18CmnViewSettingNode @ 0xfab954; typeinfo name 0xdd74dc). Confirmed to be the
     * same role as the a6400 compare (PAS_* model node vtable). Was a6400 0x16c183e8.
     * TO VERIFY: runtime vptr = CautionConfig base + 0x00fab968 (load-base rebase). */
    if (!pnode || rd32(pnode, 0u) != 0x00fab968u) { /* ZV-E10: TO VERIFY (rebase) */
        return;
    }

    /* ZV-E10: TO VERIFY — Widget::isHit is "TBD" in the blueprint (§1 row "Widget hit-test /
     * binding / refresh ..."; no address exists yet). a6400 constant kept. */
    int (*is_hit)(uint32_t, const struct mt_point *) = (void *)0x6305081u; /* ZV-E10: TO VERIFY */
    /* ZV-E10: TO VERIFY — cancel_popup TBD in blueprint; no address. a6400 constant kept. */
    void (*cancel_popup)(uint32_t) = (void *)0x15b9184du; /* ZV-E10: TO VERIFY */
    /* ZV-E10: candidate PAS_MenuListPopup::setPopupPrev @ 0x861b4e / setPopupNext @ 0x861b78
     * (PAS_MenuListPopup exports; moveHlt @ 0x88db10). a6400 popup_up/popup_down scrolled the
     * popup by one row; direction-to-Prev/Next and `menu`-vs-`popup` `this` are unverified.
     * TO VERIFY. */
    void (*popup_up)(uint32_t) = (void *)0x861b4eu; /* ZV-E10: TO VERIFY (candidate setPopupPrev) */
    void (*popup_down)(uint32_t) = (void *)0x861b78u; /* ZV-E10: TO VERIFY (candidate setPopupNext) */
    /* ZV-E10: TO VERIFY — no blueprint equivalent. Candidates: CmnViewSettingNode idx47
     * setItemSelectedByIndex (address not given) / idx48 setItemSelectedBySRNum @ 0xb8e60c
     * (file offset; SRNum semantics differ from a raw index). Verify against live symbol dump. */
    void (*set_popup_row)(uint32_t, uint32_t) = (void *)0x15b96f11u; /* ZV-E10: TO VERIFY */
    /* ZV-E10: TO VERIFY — confirm_popup TBD in blueprint; no address. a6400 constant kept. */
    void (*confirm_popup)(uint32_t) = (void *)0x15b9638bu; /* ZV-E10: TO VERIFY */
    /* ZV-E10: TO VERIFY — apply_popup TBD in blueprint; no address. a6400 constant kept. */
    void (*apply_popup)(uint32_t) = (void *)0x157f8ca5u; /* ZV-E10: TO VERIFY */
    uint32_t view = find_setting_menu_view(menu);

    unsigned total = rd32(pnode, 8u);
    unsigned vis_cnt = rd8(popup, 0x6480u);
    if (total < 1u || total > 128u || vis_cnt < 1u || vis_cnt > MT_MAX_ROWS) {
        return;
    }

    if (has_press && iabs(dy) >= 36 && iabs(dy) > iabs(dx)) {
        uint32_t dir = dy < 0 ? 1u : 0u;
        if (dir) {
            popup_down(menu);
        } else {
            popup_up(menu);
        }
        record_event(s, MT_ACT_POPUP_SCROLL, dir, x, y, 0u, 0u, rd8(popup, 0x648du));
        return;
    }

    unsigned top = rd8(popup, 0x648cu);
    struct mt_point pt = {(float)x, (float)y};

    for (unsigned r = 0; r < vis_cnt; r++) {
        unsigned opt = top + r + 1u;
        if (opt > total) {
            break;
        }
        if (is_hit(popup + 0x1070u + r * 0x398u, &pt) ||
            is_hit(popup + 0x36a8u + r * 0x220u, &pt)) {
            set_popup_row(popup, r);
            *(volatile uint32_t *)(menu + 0x1fcu) = top + r;
            if (view) {
                apply_popup(view);
            } else {
                confirm_popup(popup);
            }
            record_event(s, MT_ACT_POPUP_SELECT, opt, x, y, view, top, r + 1u);
            return;
        }
    }

    if ((!is_hit(popup + 0x64cu, &pt) || x < 80 || x > 560 || y < 32 || y > 456) &&
        !is_hit(popup + 0x60d8u, &pt) && !is_hit(popup + 0x5c04u, &pt) &&
        !is_hit(popup + 0x2780u, &pt)) {
        if (view) {
            push_async_event(0x15010102u, 4u); /* framework KEY_MENU-ish event id — unchanged (not an address) */
        } else {
            cancel_popup(menu);
        }
        record_event(s, MT_ACT_POPUP_CANCEL, 0u, x, y, view, 0u, rd8(popup, 0x648du));
    }
}

static void handle_menu_touch(volatile struct mt_full_state *s, uint32_t mouse) {
    int16_t x = 0, y = 0;
    /* ZV-E10: TO VERIFY — a6400 WidgetSystem mouse-position getter @ 0x6309019; not mapped
     * in the blueprint. */
    void (*get_pos)(uint32_t, unsigned, int16_t *, int16_t *) = (void *)0x6309019u; /* ZV-E10: TO VERIFY */
    get_pos(MT_SYSTEM_ADDR, mouse, &x, &y);

    int has_press = (s->press_valid != 0u);
    int dx = (int)x - s->press_x;
    int dy = (int)y - s->press_y;
    s->press_valid = 0u;

    if (x == 32767 || y == 32767) {
        return;
    }

    uint32_t err = ~0u;
    /* ZV-E10: TO VERIFY — a6400 module/object lookup (hash-key registry) @ 0x62e27df;
     * not mapped in the blueprint. The hash-key arguments 0xaadca2c7 / 0xdfdc8ddd /
     * 0xa056f604 are FRAMEWORK CLASS-HASH CONSTANTS (same CE 8 framework, same C++ class
     * names) and are expected to be identical on the ZV-E10 — but this is unverified. */
    uint32_t (*lookup)(uint32_t, uint32_t, uint32_t, uint32_t *) = (void *)0x62e27dfu; /* ZV-E10: TO VERIFY */
    uint32_t menu = lookup(0xaadca2c7u, 0xdfdc8dddu, 0xa056f604u, &err); /* framework class hashes — expected unchanged */
    /* ZV-E10: PAS_MenuListTab vtable address point 0xc01e28 (_ZTV @ 0xc01e20; RTTI @ 0xc01fe8;
     * typeinfo name "15PAS_MenuListTab" @ 0xa8b2a0). Was a6400 0x15f1f1d0. High confidence. */
    if (err || !menu || rd32(menu, 0u) != 0xc01e28u || rd32(menu, 0x1cu) != menu) {
        return;
    }

    /* ZV-E10: TO VERIFY — main_state / inherited_mode / popup_open have no blueprint
     * equivalents (§1 row "main_state ...": TBD). a6400 constants kept. */
    int (*main_state)(uint32_t) = (void *)0x15b90535u; /* ZV-E10: TO VERIFY */
    int (*inherited_mode)(uint32_t) = (void *)0x631e815u; /* ZV-E10: TO VERIFY */
    int (*popup_open)(uint32_t) = (void *)0x15b95b61u; /* ZV-E10: TO VERIFY */
    if (rd8(menu, 0x21u) != 2u || inherited_mode(menu) > 2) {
        return;
    }

    if (popup_open(menu + 0xfb20u)) {
        if (s->enabled >= 2u) {
            handle_popup_touch(s, menu, x, y, dx, dy, has_press);
        }
        return;
    }

    if (!main_state(menu)) {
        return;
    }

    if (s->enabled < 2u) {
        record_event(s, MT_ACT_NONE, 0u, x, y, menu, 0u, 0u);
        return;
    }

    /* menu+0x186ac root-tree pointer — struct layout CONFIRMED byte-identical on ZV-E10
     * (blueprint §0). The tree node behind it is CmnViewSettingNode. */
    uint32_t root = rd32(menu, 0x186acu);
    if (!root || rd32(root, 0u) != 0x00fab968u) { /* ZV-E10: CmnViewSettingNode vtable VA — TO VERIFY (rebase) */
        return;
    }

    /* ZV-E10: TO VERIFY — selected_index. Candidate: PAS "getHighlightItemPos" @ 0x860b36,
     * but the a6400 form is (node, unsigned *out) -> int 1; the ZV-E10 signature/class is
     * not verified. a6400 constant kept. */
    int (*selected_index)(uint32_t, unsigned *) = (void *)0x16925c17u; /* ZV-E10: TO VERIFY (candidate 0x860b36 getHighlightItemPos) */
    /* ZV-E10: TO VERIFY — indexed_child has no blueprint equivalent. a6400 constant kept. */
    int (*indexed_child)(uint32_t, unsigned, uint32_t *) = (void *)0x16925bc7u; /* ZV-E10: TO VERIFY */
    /* ZV-E10: TO VERIFY — set_selected_index. Candidates: CmnViewSettingNode idx47
     * setItemSelectedByIndex (address not given) / idx48 setItemSelectedBySRNum @ 0xb8e60c
     * (file offset; SRNum semantics differ from a raw index). Verify against live symbols. */
    int (*set_selected_index)(uint32_t, unsigned) = (void *)0x169260dbu; /* ZV-E10: TO VERIFY */
    /* ZV-E10: TO VERIFY — Widget::isHit TBD in blueprint; a6400 constant kept. */
    int (*is_hit)(uint32_t, const struct mt_point *) = (void *)0x6305081u; /* ZV-E10: TO VERIFY */
    /* ZV-E10: enter_selected analog — PAS_MenuListTab::openLevel2Popup @ 0x86292c
     * (called from 0x303260 in the ZV-E10 Action flow). Was a6400 0x15b92045.
     * Signature (menu, leaf) UNVERIFIED — openLevel2Popup may expect only `this`. */
    int (*enter_selected)(uint32_t, uint32_t) = (void *)0x86292cu; /* ZV-E10: TO VERIFY (openLevel2Popup @ 0x86292c) */

    unsigned tabs = rd32(root, 8u), tab_sel = 0;
    if (tabs < 1u || tabs > MT_MAX_TABS ||
        selected_index(root, &tab_sel) != 1 || tab_sel < 1u || tab_sel > tabs) {
        return;
    }

    struct mt_point pt = {(float)x, (float)y};
    int is_swipe = has_press && iabs(dx) >= 48 && iabs(dx) > 2 * iabs(dy);

    /* Tab taps need only the real root: they work with tab-bar focus (menu+0x1d8) and in tabs whose
     * content is not a plain setting tree (My Menu, Mods). Rows/pages/swipe keep the full checks below. */
    if (!is_swipe) {
        for (unsigned i = 0; i < tabs; i++) {
            if (is_hit(menu + 0x0b9cu + i * 0x220u, &pt)) {
                if (i + 1u != tab_sel) {
                    if (set_selected_index(root, i + 1u) == 1) {
                        sync_refresh(menu);
                    } else {
                        s->last_err = 1u;
                    }
                }
                record_event(s, MT_ACT_TAB, i + 1u, x, y, tab_sel, 0u, 0u);
                return;
            }
        }
    }

    if (rd8(menu, 0x1d8u)) {
        return; /* tab-bar focus: only tab taps are handled */
    }

    uint32_t tab_node = 0;
    /* ZV-E10: CmnViewSettingNode vtable VA (CautionConfig.so, file 0xfa3968) — was a6400 0x16c183e8. */
    if (indexed_child(root, tab_sel, &tab_node) != 1 || !tab_node ||
        rd32(tab_node, 0u) != 0x00fab968u) { /* ZV-E10: TO VERIFY (rebase) */
        return;
    }

    unsigned pages = rd32(tab_node, 8u), page_sel = 0;
    uint32_t page_node = 0;
    if (pages < 1u || pages > MT_MAX_PAGES ||
        selected_index(tab_node, &page_sel) != 1 || page_sel < 1u || page_sel > pages ||
        indexed_child(tab_node, page_sel, &page_node) != 1 || !page_node ||
        rd32(page_node, 0u) != 0x00fab968u) { /* ZV-E10: CmnViewSettingNode vtable VA — TO VERIFY (rebase) */
        return;
    }

    unsigned rows = rd32(page_node, 8u), row_sel = 0;
    if (rows < 1u || rows > MT_MAX_ROWS ||
        selected_index(page_node, &row_sel) != 1 || row_sel < 1u || row_sel > rows ||
        rd32(menu, 0x1f8u) != row_sel - 1u) {
        return;
    }

    /* Horizontal swipe: left = next page/tab, right = prev page/tab */
    if (is_swipe) {
        if (dx < 0) {
            if (page_sel < pages) {
                set_selected_index(tab_node, page_sel + 1u);
            } else {
                set_selected_index(root, tab_sel < tabs ? tab_sel + 1u : 1u);
            }
        } else {
            if (page_sel > 1u) {
                set_selected_index(tab_node, page_sel - 1u);
            } else {
                set_selected_index(root, tab_sel > 1u ? tab_sel - 1u : tabs);
            }
        }
        sync_refresh(menu);
        record_event(s, MT_ACT_SWIPE_PAGE, dx < 0 ? 1u : 2u, x, y, tab_sel, page_sel, row_sel);
        return;
    }

    for (unsigned p = 0; p < pages; p++) {
        if (is_hit(menu + 0x166b4u + p * 0x220u, &pt)) {
            if (p + 1u != page_sel) {
                if (set_selected_index(tab_node, p + 1u) == 1) {
                    sync_refresh(menu);
                } else {
                    s->last_err = 2u;
                }
            }
            record_event(s, MT_ACT_PAGE, p + 1u, x, y, tab_sel, page_sel, row_sel);
            return;
        }
    }

    for (unsigned r = 0; r < rows; r++) {
        /* ZV-E10: row stride 0x1118 UNVERIFIED (blueprint §2.6). */
        if (is_hit(menu + 0x9350u + r * 0x1118u, &pt)) {
            if (r + 1u != row_sel) {
                if (set_selected_index(page_node, r + 1u) == 1) {
                    sync_refresh(menu);
                } else {
                    s->last_err = 3u;
                }
                record_event(s, MT_ACT_ROW_SELECT, r + 1u, x, y, tab_sel, page_sel, row_sel);
            } else {
                uint32_t view = find_setting_menu_view(menu);
                if (view) {
                    push_async_event(0x15010052u, 4u); /* framework KEY_ENTER-ish event id — unchanged (not an address) */
                } else {
                    uint32_t leaf = 0;
                    if (indexed_child(page_node, r + 1u, &leaf) == 1 && leaf) {
                        enter_selected(menu, leaf);
                    } else {
                        s->last_err = 4u;
                    }
                }
                record_event(s, MT_ACT_ROW_ENTER, r + 1u, x, y, view, page_sel, row_sel);
            }
            return;
        }
    }
}

uint32_t mt_full_press(void *system, void *message) {
    volatile struct mt_full_state *s = (volatile struct mt_full_state *)MT_STATE_ADDR;
    if (s->magic != MT_FULL_MAGIC || s->version != MT_FULL_VERSION) {
        return mt_original_press(system, message);
    }
    __sync_fetch_and_add(&s->inflight, 1u);
    uint32_t result = mt_original_press(system, message);
    if (s->enabled && (uint32_t)system == MT_SYSTEM_ADDR && message) {
        unsigned mouse = ((const uint8_t *)message)[0x14];
        if (mouse < 4u) {
            int16_t px = 0, py = 0;
            /* ZV-E10: TO VERIFY — get_pos @ 0x6309019 (a6400); not mapped in blueprint. */
            void (*get_pos)(uint32_t, unsigned, int16_t *, int16_t *) = (void *)0x6309019u; /* ZV-E10: TO VERIFY */
            get_pos(MT_SYSTEM_ADDR, mouse, &px, &py);
            if (px != 32767 && py != 32767) {
                s->press_x = px;
                s->press_y = py;
                s->press_valid = 1u;
            }
        }
    }
    __sync_fetch_and_sub(&s->inflight, 1u);
    return result;
}

uint32_t mt_full_release(void *system, void *message) {
    volatile struct mt_full_state *s = (volatile struct mt_full_state *)MT_STATE_ADDR;
    if (s->magic != MT_FULL_MAGIC || s->version != MT_FULL_VERSION) {
        return mt_original_release(system, message);
    }
    __sync_fetch_and_add(&s->inflight, 1u);
    uint32_t result = mt_original_release(system, message);
    if (s->enabled && (uint32_t)system == MT_SYSTEM_ADDR && message) {
        unsigned mouse = ((const uint8_t *)message)[0x14];
        if (mouse < 4u) {
            handle_menu_touch(s, mouse);
        } else {
            s->last_err = 99u;
        }
    }
    __sync_fetch_and_sub(&s->inflight, 1u);
    return result;
}
