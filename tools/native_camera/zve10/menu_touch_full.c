/* Guarded full-menu touch prototype — ZV-E10 port:
 * - Main menu (PAS_MenuListTab): tap tabs, page dots, rows (1st=select, 2nd=enter),
 *   plus horizontal swipe left/right for next/prev page.
 * - Popup menu (PAS_MenuListPopup): tap row to select+confirm, vertical swipe to scroll,
 *   tap outside popup panel to cancel/close.
 *
 * ZV-E10 (BIONZ X / CXD90045, CE Linux 8.2.7.6, FW 2.02 / 2.03).
 * Verified against on-device firmware dump symbols and disassembly.
 * RAM-only injection via ptrace into libObj.so RX zero padding (0x011964f0).
 * Dynamic base addresses resolved via state block in libObj.so .data (0x013f8000).
 */
#include <stdint.h>

#define MT_FULL_MAGIC 0x4d544632u /* "MTF2" */
#define MT_FULL_VERSION 2u

/* libObj.so relative offsets */
#define MT_SYSTEM_ADDR       0x0145cb1cu  /* WidgetSystem instance in .bss */
#define MT_OFFSET_GET_POS    0x006172cdu  /* WidgetSystem::getMousePosInFramebuffer */
#define MT_OFFSET_LOOKUP     0x005f05afu  /* WidgetLayouter::getWidget */
#define MT_OFFSET_IS_HIT     0x00613335u  /* Widget::isHit */
#define MT_OFFSET_INHERITED  0x00613bd1u  /* Widget::calcFinalMode */
#define MT_OFFSET_CREATE_EVT 0x0012c651u  /* Event factory: create_event(ulong id, u8 type) */
#define MT_OFFSET_PUSH_EVT   0x0012c62du  /* Event dispatcher: push_event(Event* ev) */
#define MT_OFFSET_P_VIEW_MGR 0x0149221cu  /* pViewManager (.bss in libObj.so) */

/* viewUnified2.so relative offsets */
#define MT_OFFSET_VTV_PAS_TAB 0x00c09e28u /* PAS_MenuListTab vtable addr pt (verified live: 0x168d8e28 - 0x15ccf000) */
#define MT_OFFSET_ENTER       0x0086292du /* PAS_MenuListTab::openLevel2Popup */
#define MT_OFFSET_POPUP_PREV  0x00861b4fu /* PAS_MenuListTab::setPopupPrev */
#define MT_OFFSET_POPUP_NEXT  0x00861b79u /* PAS_MenuListTab::setPopupNext */
#define MT_OFFSET_REFRESH     0x008629f9u /* PAS_MenuListTab::refresh */
#define MT_OFFSET_CLOSE_POPUP 0x008620e1u /* PAS_MenuListTab::closeMenuPopup (closes popup, restores main_state=2, refreshes menu) */
#define MT_OFFSET_VTV_SETTING_MENU 0x00ba2c8cu /* ViewSettingMenu primary vtable addr pt (viewUnified2.so) */
#define MT_OFFSET_ACTION_DISPATCH  0x0030412bu /* ViewSettingMenu::ProductAction(this, action_id) */

/* CautionConfig.so relative offsets */
#define MT_OFFSET_VTV_NODE    0x00fab968u /* CmnViewSettingNode vtable addr pt */
#define MT_OFFSET_SEL_INDEX   0x00b8e127u /* CmnViewSettingNode::getIndexSelectedItem */
#define MT_OFFSET_IDX_CHILD   0x00b8e0cbu /* CmnViewSettingNode::getSubItemByIndex */
#define MT_OFFSET_SET_INDEX   0x00b8e5ebu /* CmnViewSettingNode::setItemSelectedByIndex */

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
#define MT_MAX_TABS 6u
#endif
#define MT_MAX_PAGES 15u
#define MT_MAX_ROWS 6u

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
    uint32_t lib_base;
    uint32_t view_base;
    uint32_t caution_base;
    uint32_t cached_menu;
    struct mt_full_event events[MT_EVENT_CAP];
};

extern uint32_t mt_original_release(void *, void *) __attribute__((visibility("hidden")));
extern uint32_t mt_original_press(void *, void *) __attribute__((visibility("hidden")));
extern volatile struct mt_full_state *mt_get_state_ptr(void) __attribute__((visibility("hidden")));

static inline volatile struct mt_full_state *get_state(void) {
    return mt_get_state_ptr();
}

static inline uint32_t rd32(uint32_t addr, unsigned off) {
    return *(const volatile uint32_t *)(addr + off);
}

static inline unsigned rd8(uint32_t addr, unsigned off) {
    return *(const volatile uint8_t *)(addr + off);
}

static uint32_t get_tid(void) {
    register uint32_t r0 __asm__("r0") = 0;
    register uint32_t r7 __asm__("r7") = 224;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r7) : "memory", "cc");
    return r0;
}

static inline int iabs(int v) {
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

static void sync_refresh(volatile struct mt_full_state *s, uint32_t menu) {
    void (*refresh)(uint32_t, uint32_t) = (void *)(s->view_base + MT_OFFSET_REFRESH);
    refresh(menu, 0u);
}

static void push_async_event(volatile struct mt_full_state *s, uint32_t event_id, uint8_t ev_type) {
    if (!event_id) {
        return;
    }
    void *(*create_evt)(uint32_t, uint8_t) = (void *)(s->lib_base + MT_OFFSET_CREATE_EVT);
    void (*push_evt)(void *) = (void *)(s->lib_base + MT_OFFSET_PUSH_EVT);
    void *ev = create_evt(event_id, ev_type);
    if (!ev) {
        return;
    }
    push_evt(ev);
}

static uint32_t find_setting_menu_view(volatile struct mt_full_state *s, uint32_t menu) {
    uint32_t vid = rd32(menu, 0x14cu);
    uint32_t vm = rd32(s->lib_base + MT_OFFSET_P_VIEW_MGR, 0u);
    if (!vm) {
        return 0u;
    }
    int cnt = (int)rd32(vm, 0u);
    uint32_t entries = rd32(vm, 0x20u);
    if (!entries || cnt <= 0 || cnt > 128) {
        return 0u;
    }
    uint32_t vtv = s->view_base + MT_OFFSET_VTV_SETTING_MENU;
    for (int i = 0; i < cnt; i++) {
        uint32_t ent = entries + (uint32_t)i * 16u;
        uint32_t vwrap = rd32(ent, 4u);
        if (!vwrap || rd32(ent, 8u) != vid) {
            continue;
        }
        if (rd32(vwrap, 0u) == vtv) {
            return vwrap;
        }
        uint32_t inner = rd32(vwrap, 0x18u);
        if (inner && rd32(inner, 0u) == vtv) {
            return inner;
        }
        /* Fallback: if vid matched, vwrap is our owning view instance */
        return vwrap;
    }
    return 0u;
}

static void handle_popup_touch(volatile struct mt_full_state *s, uint32_t menu,
                               int16_t x, int16_t y, int dx, int dy, int has_press) {
    uint32_t lib = s->lib_base;
    uint32_t view = s->view_base;
    uint32_t caution = s->caution_base;

    uint32_t popup = menu + 0xfb20u;
    uint32_t pnode = rd32(popup, 0x6490u);
    if (!pnode) {
        s->last_err = 201u; /* popup node null */
        return;
    }
    uint32_t pvtv = rd32(pnode, 0u);
    /* In C++, setting items use derived vtables located in CautionConfig.so (.data.rel.ro / .data) */
    if (pvtv < caution || pvtv >= (caution + 0x01220000u)) {
        s->last_err = 202u; /* popup node vtable mismatch */
        s->last_target = pvtv;
        return;
    }

    int (*is_hit)(uint32_t, const struct mt_point *) = (void *)(lib + MT_OFFSET_IS_HIT);
    void (*close_popup)(uint32_t) = (void *)(view + MT_OFFSET_CLOSE_POPUP);
    void (*popup_up)(uint32_t) = (void *)(view + MT_OFFSET_POPUP_PREV);
    void (*popup_down)(uint32_t) = (void *)(view + MT_OFFSET_POPUP_NEXT);

    unsigned total = rd32(pnode, 8u);
    unsigned vis_cnt = rd8(popup, 0x6480u);
    if (total < 1u || total > 128u || vis_cnt < 1u || vis_cnt > MT_MAX_ROWS) {
        s->last_err = 203u; /* popup counts out of range */
        s->last_target = (total << 16) | vis_cnt;
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

    int (*set_node_index)(uint32_t, unsigned) = (void *)(caution + MT_OFFSET_SET_INDEX);
    unsigned top = rd8(popup, 0x648cu);
    struct mt_point pt = {(float)x, (float)y};

    for (unsigned r = 0; r < vis_cnt; r++) {
        unsigned opt = top + r + 1u;
        if (opt > total) {
            break;
        }
        if (is_hit(popup + 0x1070u + r * 0x398u, &pt) ||
            is_hit(popup + 0x36a8u + r * 0x220u, &pt)) {
            *(volatile uint32_t *)(menu + 0x1fcu) = top + r;
            set_node_index(pnode, opt);
            close_popup(menu);
            record_event(s, MT_ACT_POPUP_SELECT, opt, x, y, 0u, top, r + 1u);
            return;
        }
    }

    if ((!is_hit(popup + 0x64cu, &pt) || x < 80 || x > 560 || y < 32 || y > 456) &&
        !is_hit(popup + 0x60d8u, &pt) && !is_hit(popup + 0x5c04u, &pt) &&
        !is_hit(popup + 0x2780u, &pt)) {
        close_popup(menu);
        record_event(s, MT_ACT_POPUP_CANCEL, 0u, x, y, 0u, 0u, rd8(popup, 0x648du));
        return;
    }

    s->last_err = 204u; /* popup touch missed hit targets */
    s->last_target = ((uint32_t)(uint16_t)x << 16) | (uint16_t)y;
}

static void handle_menu_touch(volatile struct mt_full_state *s, uint32_t mouse) {
    uint32_t lib = s->lib_base;
    uint32_t view = s->view_base;
    uint32_t caution = s->caution_base;

    int16_t x = 0, y = 0;
    void (*get_pos)(uint32_t, unsigned, int16_t *, int16_t *) = (void *)(lib + MT_OFFSET_GET_POS);
    get_pos(lib + MT_SYSTEM_ADDR, mouse, &x, &y);

    int has_press = (s->press_valid != 0u);
    int dx = (int)x - s->press_x;
    int dy = (int)y - s->press_y;
    s->press_valid = 0u;

    if (x == 32767 || y == 32767) {
        s->last_err = 99u; /* invalid coordinate */
        return;
    }

    s->last_x = x;
    s->last_y = y;
    s->last_action = 100u; /* reached handle_menu_touch */

    uint32_t expected_vtv = view + MT_OFFSET_VTV_PAS_TAB;
    uint32_t err = ~0u;
    uint32_t (*lookup)(uint32_t, uint32_t, uint32_t, uint32_t *) = (void *)(lib + MT_OFFSET_LOOKUP);
    uint32_t menu = lookup(0xaadca2c7u, 0xdfdc8dddu, 0xa056f604u, &err);
    if (!err && menu) {
        s->cached_menu = menu;
    } else {
        /* If lookup failed because popup/modal dialog is active, fallback to cached menu */
        uint32_t cached = s->cached_menu;
        if (cached > 0x10000000u && rd32(cached, 0u) == expected_vtv && rd32(cached, 0x1cu) == cached) {
            menu = cached;
            err = 0;
        } else {
            s->last_err = 101u; /* lookup failed */
            s->last_target = err;
            return;
        }
    }
    s->last_target = menu;
    uint32_t menu_vtv = rd32(menu, 0u);
    if (menu_vtv != expected_vtv) {
        s->last_err = 103u; /* menu vtable mismatch */
        s->last_target = menu_vtv;
        return;
    }
    if (rd32(menu, 0x1cu) != menu) {
        s->last_err = 104u; /* self pointer mismatch */
        s->last_target = rd32(menu, 0x1cu);
        return;
    }

    int (*inherited_mode)(uint32_t) = (void *)(lib + MT_OFFSET_INHERITED);
    uint8_t m21 = rd8(menu, 0x21u);
    int inh = inherited_mode(menu);
    if (m21 != 2u) {
        s->last_err = 105u; /* mode byte != 2 */
        s->last_target = (uint32_t)m21;
        return;
    }
    if (inh > 2) {
        s->last_err = 106u; /* inherited_mode > 2 */
        s->last_target = (uint32_t)inh;
        return;
    }

    uint32_t m_state = rd32(menu, 0x1e0u);
    uint8_t popup_open_byte = rd8(menu + 0xfb20u, 0x648eu);
    if (m_state == 3u || popup_open_byte != 0u) { /* popup_open */
        s->last_action = 110u; /* in popup */
        if (s->enabled >= 2u) {
            handle_popup_touch(s, menu, x, y, dx, dy, has_press);
        }
        return;
    }

    if (m_state != 2u) {
        s->last_err = 107u; /* main_state != 2 */
        s->last_target = m_state;
        return;
    }

    if (s->enabled < 2u) {
        record_event(s, MT_ACT_NONE, 0u, x, y, menu, 0u, 0u);
        return;
    }

    uint32_t root = rd32(menu, 0x186acu);
    if (!root) {
        s->last_err = 108u; /* root node null */
        return;
    }
    uint32_t root_vtv = rd32(root, 0u);
    uint32_t expected_node = caution + MT_OFFSET_VTV_NODE;
    if (root_vtv != expected_node) {
        s->last_err = 109u; /* root node vtable mismatch */
        s->last_target = root_vtv;
        return;
    }

    int (*selected_index)(uint32_t, unsigned *) = (void *)(caution + MT_OFFSET_SEL_INDEX);
    int (*indexed_child)(uint32_t, unsigned, uint32_t *) = (void *)(caution + MT_OFFSET_IDX_CHILD);
    int (*set_selected_index)(uint32_t, unsigned) = (void *)(caution + MT_OFFSET_SET_INDEX);
    int (*is_hit)(uint32_t, const struct mt_point *) = (void *)(lib + MT_OFFSET_IS_HIT);
    int (*enter_selected)(uint32_t, uint32_t) = (void *)(view + MT_OFFSET_ENTER);

    unsigned tabs = rd32(root, 8u), tab_sel = 0;
    if (tabs < 1u || tabs > MT_MAX_TABS) {
        s->last_err = 112u; /* tabs count out of range */
        s->last_target = tabs;
        return;
    }
    if (selected_index(root, &tab_sel) != 1 || tab_sel < 1u || tab_sel > tabs) {
        s->last_err = 113u; /* tab_sel out of range */
        s->last_target = tab_sel;
        return;
    }

    s->last_action = 120u; /* reached hit tests */

    struct mt_point pt = {(float)x, (float)y};
    int is_swipe = has_press && iabs(dx) >= 48 && iabs(dx) > 2 * iabs(dy);

    if (!is_swipe) {
        for (unsigned i = 0; i < tabs; i++) {
            if (is_hit(menu + 0x0b9cu + i * 0x220u, &pt)) {
                if (i + 1u != tab_sel) {
                    if (set_selected_index(root, i + 1u) == 1) {
                        sync_refresh(s, menu);
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
        s->last_err = 114u; /* tab-bar focus active */
        return;
    }

    uint32_t tab_node = 0;
    if (indexed_child(root, tab_sel, &tab_node) != 1 || !tab_node ||
        rd32(tab_node, 0u) != (caution + MT_OFFSET_VTV_NODE)) {
        s->last_err = 115u; /* tab_node invalid */
        return;
    }

    unsigned pages = rd32(tab_node, 8u), page_sel = 0;
    uint32_t page_node = 0;
    if (pages < 1u || pages > MT_MAX_PAGES ||
        selected_index(tab_node, &page_sel) != 1 || page_sel < 1u || page_sel > pages ||
        indexed_child(tab_node, page_sel, &page_node) != 1 || !page_node ||
        rd32(page_node, 0u) != (caution + MT_OFFSET_VTV_NODE)) {
        s->last_err = 116u; /* page_node invalid */
        return;
    }

    unsigned rows = rd32(page_node, 8u), row_sel = 0;
    if (rows < 1u || rows > MT_MAX_ROWS ||
        selected_index(page_node, &row_sel) != 1 || row_sel < 1u || row_sel > rows) {
        s->last_err = 117u; /* row selection invalid */
        return;
    }
    if (rd32(menu, 0x1f8u) != row_sel - 1u) {
        *(volatile uint32_t *)(menu + 0x1f8u) = row_sel - 1u;
    }

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
        sync_refresh(s, menu);
        record_event(s, MT_ACT_SWIPE_PAGE, dx < 0 ? 1u : 2u, x, y, tab_sel, page_sel, row_sel);
        return;
    }

    for (unsigned p = 0; p < pages; p++) {
        if (is_hit(menu + 0x166b4u + p * 0x220u, &pt)) {
            if (p + 1u != page_sel) {
                if (set_selected_index(tab_node, p + 1u) == 1) {
                    sync_refresh(s, menu);
                } else {
                    s->last_err = 2u;
                }
            }
            record_event(s, MT_ACT_PAGE, p + 1u, x, y, tab_sel, page_sel, row_sel);
            return;
        }
    }

    for (unsigned r = 0; r < rows; r++) {
        if (is_hit(menu + 0x9350u + r * 0x1118u, &pt)) {
            if (r + 1u != row_sel) {
                if (set_selected_index(page_node, r + 1u) == 1) {
                    *(volatile uint32_t *)(menu + 0x1f8u) = r;
                    *(volatile uint32_t *)(menu + 0x1fcu) = r;
                    sync_refresh(s, menu);
                } else {
                    s->last_err = 3u;
                }
                record_event(s, MT_ACT_ROW_SELECT, r + 1u, x, y, tab_sel, page_sel, row_sel);
            } else {
                uint32_t leaf = 0;
                int is_popup = 0;
                if (indexed_child(page_node, r + 1u, &leaf) == 1 && leaf) {
                    int (*get_selectable)(uint32_t, int *) = (void *)rd32(rd32(leaf, 0u), 0x10u);
                    int sel_count = 0;
                    if (get_selectable && get_selectable(leaf, &sel_count) == 1 && sel_count > 0) {
                        is_popup = 1;
                    }
                }
                set_selected_index(page_node, r + 1u);
                *(volatile uint32_t *)(menu + 0x1f8u) = r;
                *(volatile uint32_t *)(menu + 0x1fcu) = r;
                *(volatile uint8_t *)(menu + 0x1d8u) = 0;

                if (is_popup) {
                    enter_selected(menu, leaf);
                } else {
                    uint32_t sm_view = find_setting_menu_view(s, menu);
                    if (sm_view) {
                        s->last_err = 110u; /* Action 11 dispatched */
                        void (*action)(uint32_t, int) = (void *)(s->view_base + MT_OFFSET_ACTION_DISPATCH);
                        action(sm_view, 11);
                    } else {
                        s->last_err = 111u; /* sm_view not found */
                        push_async_event(s, 0x15010052u, 2u);
                    }
                }
                record_event(s, MT_ACT_ROW_ENTER, r + 1u, x, y, (uint32_t)is_popup, page_sel, row_sel);
            }
            return;
        }
    }

    s->last_err = 199u; /* no widget hit at coordinates */
}

uint32_t mt_full_press(void *system, void *message) {
    volatile struct mt_full_state *s = get_state();
    if (s->magic != MT_FULL_MAGIC || s->version != MT_FULL_VERSION) {
        return mt_original_press(system, message);
    }
    __sync_fetch_and_add(&s->inflight, 1u);
    uint32_t result = mt_original_press(system, message);
    if (s->enabled && message) {
        unsigned mouse = ((const uint8_t *)message)[0x14];
        if (mouse < 4u) {
            int16_t px = 0, py = 0;
            void (*get_pos)(uint32_t, unsigned, int16_t *, int16_t *) = (void *)(s->lib_base + MT_OFFSET_GET_POS);
            get_pos(s->lib_base + MT_SYSTEM_ADDR, mouse, &px, &py);
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
    volatile struct mt_full_state *s = get_state();
    if (s->magic != MT_FULL_MAGIC || s->version != MT_FULL_VERSION) {
        return mt_original_release(system, message);
    }
    __sync_fetch_and_add(&s->inflight, 1u);
    uint32_t result = mt_original_release(system, message);
    if (s->enabled && message) {
        unsigned mouse = ((const uint8_t *)message)[0x14];
        if (mouse < 4u) {
            handle_menu_touch(s, mouse);
        } else {
            s->last_err = 98u; /* mouse index >= 4 */
        }
    }
    __sync_fetch_and_sub(&s->inflight, 1u);
    return result;
}
