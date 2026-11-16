# ZV-E10 `MenuTouchFull` (MTF2) — first-pass port

**Status: structural first-pass port only. UNTESTED ON HARDWARE. No camera was accessed,
no firmware was modified, no binary was run. The output of `build_zve10.sh` is a
cross-compile check, not a deployable mod.**

Source of truth: [`../../../docs/porting/zv-e10/zv-e10-touch-mod-blueprint.md`](../../../docs/porting/zv-e10/zv-e10-touch-mod-blueprint.md)
(ZV-E10 FW 2.03, offline RE 2026-11-16). Same BIONZ X / CXD90045 / CE Linux 8 framework as
the ILCE-6400; struct layouts verified byte-identical for the parts that matter
(`menu + 0x186ac` root-tree pointer, `PAS_MenuListTab` shape).

## Address model (read this first)

Blueprint §1 addresses are **file offsets / ELF VADRs**, not runtime addresses.

- `.text`: file == vaddr.
- Second segment (`.data.rel.ro`/`.got`/`.data`/`.bss`): `vaddr = file + 0x8000`
  (CautionConfig.so: `+0x7000`, e.g. CmnViewSettingNode vtable `file 0xfa3968 → VA 0xfab968`).
- Runtime address = **load base + offset**; the load base is unknown until boot.

The a6400 payload compiled absolute runtime addresses into the binary (its load bases
were known/fixed). This port substitutes the blueprint values 1:1 so every replacement
point is explicit, but it is **position-dependent and not bootable**: an installer must
either patch/rebase the constants from `/proc/<pid>/maps` at install time, or the payload
must be rebuilt with the maps-derived bases once they are known. Struct offsets
(`0x186ac`, `0x9350/0x1118`, strides, `0x1f8`, ...) are load-base independent.

## Files

| File | Origin | Change |
| --- | --- | --- |
| `menu_touch_full.c` | `../menu_touch_full.c` | Addresses re-mapped per blueprint; `ZV-E10: TO VERIFY` markers for unmapped calls |
| `menu_touch_full_trampoline.S` | `../menu_touch_full_trampoline.S` | Resume points now `postMouseRelease+0xc|1 = 0x00628495` and `postMousePress+0xc|1 = 0x0062842d` (12-byte prologue **UNVERIFIED**) |
| `menu_touch_native.c` | `../menu_touch_native.c` | Same address re-mapping; UI tid is now `MT_UI_TID` (default 380, a6400; unknown on ZV-E10) |
| `menu_touch_native.h` | `../menu_touch_native.h` | Comment updates only (no addresses) |
| `menu_touch_policy.c` | `../menu_touch_policy.c` | Comment updates only (no firmware addresses; logic unchanged) |
| `menu_touch_policy.h` | `../menu_touch_policy.h` | Copied unchanged |
| `native_menu_touch_full_guard.c` | `../native_menu_touch_full_guard.c` | Injection home/hooks/PID re-mapped; prologue arrays deliberately kept as a6400 placeholders (fail closed) |
| `build_zve10.sh` | new (models `../build_mod_loader_wsl.sh` and `mods/menu_touch_full/build_mtf2_wsl.sh`) | Cross-compile + guard build; header lists every address replacement |

## Address replacements (old → new)

Substitutions made from the blueprint (High confidence object identity):

```
0x956f0     WidgetSystem singleton            -> 0x0145cb1c   (.bss VA; GOT 0x013df230; sys_getInstance @ 0x00616fe4)
0x15f1f1d0  PAS_MenuListTab vtable (addr pt)  -> 0xc01e28     (_ZTV @ 0xc01e20; RTTI @ 0xc01fe8)
0x15EC8270  ViewSettingMenu vtable (addr pt)  -> 0xbaabb0     (_ZTV @ 0xbaaba8; RTTI @ 0xbaada4)
0x16c183e8  CmnViewSettingNode vtable         -> 0x00fab968   (CautionConfig.so; VA; file 0xfa3968)
0x07089984  EventManager singleton holder     -> 0x014920e0   (g_pEvtMgr, .bss VA, STT_OBJECT; GOT 0x013ded14)
0x05E14E20  Event ctor                        -> 0x008a01e8   (Event::Event(ulong,u8,u8))
0x05E15974  operator new (a6400 local)        -> 0x00108e0c   (imported _Znwj PLT; GOT 0x013d80a8)
0x05E18D5D  EventManager::push                -> 0x008a1794   (PLT 0x0010c2a8)
0x15b92045  enter_selected                    -> 0x86292c     (PAS_MenuListTab::openLevel2Popup; signature TO VERIFY)
0x15b9134d  popup_up                          -> 0x861b4e     (candidate setPopupPrev; direction/`this` TO VERIFY)
0x15b91377  popup_down                        -> 0x861b78     (candidate setPopupNext; direction/`this` TO VERIFY)
0x0700d708  CODE (a6400 RX home, absolute)    -> 0x011964ee   (libObj.so RX zero run, FILE OFFSET; 16,254 B)
0x06309d55  trampoline release resume         -> 0x00628495   (postMouseRelease+0xc|1)
0x06309e61  trampoline press resume           -> 0x0062842d   (postMousePress+0xc|1)
0x6309d48   HOOK_REL (guard)                  -> 0x00628488   (postMouseRelease impl; PLT 0x0011a128)
0x6309e54   HOOK_PRS (guard)                  -> 0x00628420   (postMousePress impl; PLT 0x001189b4)
```

Left unchanged with `// ZV-E10: TO VERIFY` (no usable blueprint equivalent / TBD):

```
0x7143920   MT_STATE_ADDR (RW state home)     — no ZV-E10 RW zero region located; a6400 placeholder
0x6309019   get_pos (WidgetSystem pos getter) — not in blueprint
0x6305081   is_hit                            — blueprint: Widget::isHit TBD
0x15b91431  binding (sync_refresh)            — blueprint: binding TBD
0x15b92111  refresh (sync_refresh)            — candidates 0x8629f8 (PAS idx108) / 0x2f5998 (post-move refresh); identity unverified
0x16925c17  selected_index                    — candidate 0x860b36 (getHighlightItemPos); signature unverified
0x16925bc7  indexed_child                     — not in blueprint
0x169260db  set_selected_index                — candidates: idx47 setItemSelectedByIndex (no addr) / idx48 setItemSelectedBySRNum @ 0xb8e60c file (SRNum != index)
0x15b90535  main_state                        — blueprint: TBD
0x631e815   inherited_mode                    — not in blueprint
0x15b95b61  popup_open                        — blueprint: TBD
0x15b9184d  cancel_popup                      — blueprint: TBD
0x15b9638b  confirm_popup                     — blueprint: TBD
0x157f8ca5  apply_popup                       — blueprint: TBD
0x15b96f11  set_popup_row                     — candidates: CmnViewSettingNode idx47/idx48 (see above)
0x62e27df   lookup (hash-key registry)        — not in blueprint
0x0713D7EC  ViewSettingMenu-instance list holder — not in blueprint
a6400 prologue bytes (guard orig_rel/orig_prs)— deliberately left as non-matching placeholders (fail closed)
pid 149     guard process pid                 — discover at runtime (-DMT_ZVE10_PID=<pid>); comm likely "im.elf" (verify)
UI tid 380  mt_native_context expected tid    — verify live; override -DMT_UI_TID=<tid>
```

Unchanged on purpose:

```
struct offsets: menu+0x1c, 0x14c, 0x1d8, 0x1f8, 0x21, menu+0x186ac (CONFIRMED), rows menu+0x9350
  stride 0x1118 UNVERIFIED, tabs menu+0x0b9c / pages menu+0x166b4 (stride 0x220) UNVERIFIED,
  popup regions menu+0x1070 (stride 0x398), 0x36a8 (stride 0x220), 0x64c, 0x60d8, 0x5c04,
  0x2780, 0xfb20, 0x1fc, popup+0x6490/0x6480/0x648c/0x648d — geometry "may match, may not"
framework event ids 0x15010052 / 0x15010102, lookup hash args 0xaadca2c7 / 0xdfdc8ddd / 0xa056f604
MT_FULL_MAGIC/PROTO_VER/STATE layout, MT_EVENT_CAP — framework constants, not addresses
```

## What is verified vs what is not

- **High confidence (blueprint):** the three libraries and class tree are the same framework;
  `PAS_MenuListTab` identity at `menu+0x186ac`; the substitution set in the first table above
  (object identities; not yet runtime-validated).
- **UNVERIFIED / TO VERIFY:** every hook prologue byte; the resume offset (`+0x0c`);
  `MT_STATE_ADDR` home; UI tid; pid; `/tmp_bt` existence on ZV-E10; popup/binding/refresh/
  is_hit/selected_index identities; ALL hit-test geometry and strides; event ids' semantics;
  load bases of all three libraries; whether `g_pEvtMgr` is dereferenced at the right depth
  (a6400 used double deref; kept as-is); CmnViewSettingNode vtable rebase (VA vs absolute).
- **Deliberately fail-closed:** the guard's `orig_rel`/`orig_prs` are the a6400 prologue
  bytes, so on a real ZV-E10 every identity check fails and no install can happen until the
  prologues and bases are re-derived.

## Next steps for the porter (in order)

1. Re-derive the ZV-E10 `postMouseRelease`/`postMousePress` prologues (size + bytes) from the
   firmware dump / Ghidra project; rewrite the trampoline mirror, the guard's `orig_rel`/
   `orig_prs`, and confirm the resume offset (`+0x0c`) — or re-design the hook splice.
2. Discover the load bases of `libObj.so`/`viewUnified2.so`/`CautionConfig.so` from a live
   (authorized) `/proc/<pid>/maps`; add the rebase step to the guard (or rebuild the payload
   with the bases baked in). Verify `.so` identity (build-id/hash), not just the name.
3. Verify pid/comm (`im.elf`?), UI tid, `/tmp_bt` transport, and `/proc/<pid>/comm` by live
   read-only observation (requires explicit user authorization; the a6400 live procedure does
   NOT transfer — see blueprint §3).
4. Locate the remaining TBD functions (`is_hit`, `main_state`, `popup_open`,
   `cancel_popup`/`confirm_popup`/`apply_popup`, binding/refresh, `indexed_child`,
   `set_selected_index` via CmnViewSettingNode idx47/idx48 (`file 0xb8e60c` =
   setItemSelectedBySRNum), `selected_index` vs `getHighlightItemPos @ 0x860b36`) and settle
   the `g_pEvtMgr` deref depth. Notable ZV-E10 anchors for this work:
   Action dispatcher TBH `0x304136` (148 cases, entry `0x30412a`; cases 2–5 = move dirs;
   idx75 `_ZTV+0x134`), move apply `0x30010c` → vtable `[vptr+0x1b4]` (idx109 `0x861e06`) then
   post-move refresh `0x2f5998`; PAS `moveHlt @ 0x88db10`; idx108 `0x8629f8`;
   ViewBase::Action(int) `libObj @ 0x001393a8` (a6400 `+0x94` → ZV-E10 `[vptr+0xa0]`, 3 extra
   ViewBase virtuals), ViewBase::Main `libObj @ 0x0013bc80`; framework machinery in
   **libObj.so** (not viewUnified2.so); CmnViewSettingNode lives in **CautionConfig.so**.
5. Find an RW zero state home (or add a loader-owned RW segment; blueprint §4.2), then the
   live phase under explicit per-incident user authorization, `op=0` check-only first, with
   removal proof — same discipline as the a6400 sessions.
6. Re-check `menu+0x1d8`/`0x21`/`0x1f8` gating, tab/page/row hit geometry and strides
   (`0x220`/`0x1118`/`0x398`) against a live menu session; they depend on initialized geometry.

## Framework anchors (not used by this payload, for the porter's reference)

```
ViewBase::Action(int)   libObj 0x001393a8   ([vptr+0xa0]; a6400 +0x94)
ViewBase::Main          libObj 0x0013bc80   (routes 0x0013bb1c; idx +0x9c)
DefRootView::ActionCheckerListBase (all exported):
  Initialize 0x00129f8e  ExecEv 0x00129fac  IsReadyExecEv 0x0012a260  IsUpdateEvent 0x0012a65c
  execCheckEi 0x0012b41c  addChecker 0x0012b47b  execActionEi 0x0012b8bc  addActionEi 0x0012b912
  DispatchEventEm 0x001355e4
Action dispatcher TBH   viewUnified2 0x304136  (entry 0x30412a; table 0x30413a; idx75 _ZTV+0x134; case1=execute; cases2-5=dirs 1/0/3/2)
Action1 (execute)       viewUnified2 0x304114  (case body 0x30426a; workers 0x303260 + 0x2f5564)
move handlers           viewUnified2 0x300204 / 0x3001f0 / 0x3001b0 / 0x300170 (dirs 1/0/3/2)
move apply              viewUnified2 0x30010c  (-> [vptr+0x1b4] idx109 0x861e06, then 0x2f5998)
PAS_MenuListTab::openLevel2Popup 0x86292c     (called from 0x303260)
PAS_MenuListPopup::moveHlt        0x88db10    (setPopupPrev 0x861b4e / setPopupNext 0x861b78)
getHighlightItemPos               0x860b36
ViewSettingMenu vtable  0xbaaba8 (_ZTV) / addr pt 0xbaabb0 / RTTI 0xbaada4 / name 0x8ef14f
PAS_MenuListTab vtable  0xc01e20 (_ZTV) / addr pt 0xc01e28 / RTTI 0xc01fe8 / name 0xa8b2a0
ViewBase vtable         libObj 0x1382ce8 (_ZTV) / addr pt 0x1382cf0
CmnViewSettingNode vtable CautionConfig VA 0x00fab968 (file 0xfa3968) / RTTI 0xfab954
EventManager            g_pEvtMgr .bss VA 0x014920e0 / GOT 0x013ded14 / EventManager::push 0x008a1794 (PLT 0x0010c2a8)
Event ctor              0x008a01e8 (Event(ulong,u8,u8); copy 0x008a021f; setParamList 0x008a0269)
_Znwj (operator new)    imported; PLT 0x00108e0c / GOT 0x013d80a8
RX zero injection homes libObj.so 0x011964ee (16,254 B, largest; ~45 KB in 15+ runs)
                        viewUnified2.so 0x00a6ea30 (12,000 B, largest; ~30 KB)
                        CautionConfig.so 0 B
/tmp_bt existence       UNVERIFIED on ZV-E10
```
