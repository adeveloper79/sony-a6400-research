/* ZV-E10 port (first pass, UNTESTED): resume points into the original
 * WidgetSystem::postMouseRelease / postMousePress implementations.
 *
 * The a6400 trampoline re-entered the original functions at hook+0x0c (12-byte spliced
 * prologue) with the Thumb bit set. The ZV-E10 hook targets are the IMPLEMENTATIONS,
 * not the PLT stubs (blueprint §1 "Hook candidates", High confidence for existence):
 *   postMouseRelease @ libObj 0x00628488  (PLT stub 0x0011a128)
 *   postMousePress   @ libObj 0x00628420  (PLT stub 0x001189b4)
 *
 * ZV-E10: TO VERIFY — the ZV-E10 prologues differ from the a6400's (`2de9 f047 88b0
 * 4ff0 1408 0122` / `... 1409 ...`) and are NOT yet known. The 12-byte splice and the
 * `push.w {r4..r10,lr}` / `mov.w r8|r9, #0x14` / `movs r2, #1` prologue shape here are
 * a6400-derived assumptions. Before any deploy: re-derive the prologue bytes of
 * postMouseRelease/postMousePress from the ZV-E10 libraries and fix BOTH this file's
 * `push.w`/`mov.w` mirrors and the guard's orig_rel/orig_prs arrays. Keeping the a6400
 * assumptions makes the guard fail closed (memcmp never matches).
 *
 * Addresses below are libObj.so FILE OFFSETS (file == vaddr for .text); runtime address
 * = libObj load base + offset (base unknown until boot). See zve10/README.md.
 *
 * Hook+0x0c | Thumb:
 *   release: 0x00628488 + 0x0c | 1 = 0x00628495
 *   press:   0x00628420 + 0x0c | 1 = 0x0062842d
 */
.syntax unified
.thumb
.section .text

.balign 4
.global mt_original_release
.hidden mt_original_release
.thumb_func
mt_original_release:
    push.w {r4,r5,r6,r7,r8,r9,r10,lr} /* a6400-derived; MUST match re-derived ZV-E10 prologue (TO VERIFY) */
    sub sp, #0x20
    mov.w r8, #0x14
    movs r2, #1
    ldr.w pc, [pc, #0]
    .word 0x00628495 /* postMouseRelease + 0x0c | Thumb — ZV-E10: TO VERIFY (12-byte prologue assumed) */

.balign 4
.global mt_original_press
.hidden mt_original_press
.thumb_func
mt_original_press:
    push.w {r4,r5,r6,r7,r8,r9,r10,lr} /* a6400-derived; MUST match re-derived ZV-E10 prologue (TO VERIFY) */
    sub sp, #0x20
    mov.w r9, #0x14
    movs r2, #1
    ldr.w pc, [pc, #0]
    .word 0x0062842d /* postMousePress + 0x0c | Thumb — ZV-E10: TO VERIFY (12-byte prologue assumed) */

.section .note.GNU-stack,"",%progbits
