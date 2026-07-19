;------------------------------------------------------------------------------
; voxel_renderer_ammx.asm
;
; AMMX helpers for the Apollo 68080 voxel renderer.
;
; The ray routine renders vertical terrain spans contiguously into a scratch
; column buffer.  The transpose routine then converts 8x8 byte tiles into the
; SAGE bitmap.
;
; in a0.l  column-major source (source[x * height + y])
; in a1.l  row-major destination
; in d0.w  width, multiple of 8
; in d1.w  height, multiple of 8
; in d2.l  destination bytes per row, multiple of 8
;------------------------------------------------------------------------------

  SECTION voxel_ammx,code

  xdef _VoxelTransposeAMMX
  xdef _VoxelRenderRayAMMX

_VoxelTransposeAMMX:
  movem.l d2-d7/a2-a4,-(sp)

  andi.l  #$0000ffff,d0
  andi.l  #$0000ffff,d1
  tst.l   d0
  beq     .done
  tst.l   d1
  beq     .done

  move.l  d0,d3
  lsr.w   #3,d3
  beq     .done
  subq.w  #1,d3                       ; eight-column blocks minus one

  move.l  d1,d5
  lsr.w   #3,d5
  beq     .done
  subq.w  #1,d5                       ; eight-row blocks minus one

  move.l  d1,d6
  lsl.l   #3,d6
  sub.l   d1,d6                       ; seven column strides
  movea.l a1,a4                       ; destination x-block at row zero

.next_x_block:
  move.w  d5,d4

.next_y_block:
  movea.l a0,a2
  load    (a2),e0
  adda.l  d1,a2
  load    (a2),e1
  adda.l  d1,a2
  load    (a2),e2
  adda.l  d1,a2
  load    (a2),e3
  adda.l  d1,a2
  load    (a2),e4
  adda.l  d1,a2
  load    (a2),e5
  adda.l  d1,a2
  load    (a2),e6
  adda.l  d1,a2
  load    (a2),e7
  addq.l  #8,a0

  ; Pair columns and interleave one byte at a time.
  vperm   #$08192A3B,e0,e1,e8
  vperm   #$4C5D6E7F,e0,e1,e9
  vperm   #$08192A3B,e2,e3,e10
  vperm   #$4C5D6E7F,e2,e3,e11
  vperm   #$08192A3B,e4,e5,e12
  vperm   #$4C5D6E7F,e4,e5,e13
  vperm   #$08192A3B,e6,e7,e14
  vperm   #$4C5D6E7F,e6,e7,e15

  ; Combine the two-row groups into four-row groups.
  vperm   #$018923AB,e8,e10,e0
  vperm   #$45CD67EF,e8,e10,e1
  vperm   #$018923AB,e9,e11,e2
  vperm   #$45CD67EF,e9,e11,e3
  vperm   #$018923AB,e12,e14,e4
  vperm   #$45CD67EF,e12,e14,e5
  vperm   #$018923AB,e13,e15,e6
  vperm   #$45CD67EF,e13,e15,e7

  ; Join the top and bottom halves.  E8-E15 are eight output rows.
  vperm   #$012389AB,e0,e4,e8
  vperm   #$4567CDEF,e0,e4,e9
  vperm   #$012389AB,e1,e5,e10
  vperm   #$4567CDEF,e1,e5,e11
  vperm   #$012389AB,e2,e6,e12
  vperm   #$4567CDEF,e2,e6,e13
  vperm   #$012389AB,e3,e7,e14
  vperm   #$4567CDEF,e3,e7,e15

  movea.l a1,a3
  store   e8,(a3)
  adda.l  d2,a3
  store   e9,(a3)
  adda.l  d2,a3
  store   e10,(a3)
  adda.l  d2,a3
  store   e11,(a3)
  adda.l  d2,a3
  store   e12,(a3)
  adda.l  d2,a3
  store   e13,(a3)
  adda.l  d2,a3
  store   e14,(a3)
  adda.l  d2,a3
  store   e15,(a3)
  adda.l  d2,a3
  movea.l a3,a1

  dbf     d4,.next_y_block

  adda.l  d6,a0                       ; advance from column x+1 to x+8
  addq.l  #8,a4
  movea.l a4,a1
  dbf     d3,.next_x_block

.done:
  movem.l (sp)+,d2-d7/a2-a4
  rts

;------------------------------------------------------------------------------
; Render one column-group ray into the column-major scratch buffer.
;
; in a0.l  const voxel_ammx_ray_t *
; in a1.l  voxel_render_stats_t *
;
; The caller still owns all floating-point ray setup and final 8x8 transpose.
; This routine replaces only the hot depth-sample loop and contiguous span fill.
;------------------------------------------------------------------------------

_VoxelRenderRayAMMX:
  movem.l d2-d7/a2-a6,-(sp)
  lea     -12(sp),sp
  move.l  a1,4(sp)                    ; stats pointer
  movea.l a0,a6                       ; ray context

  movea.l 0(a6),a0                    ; height map
  movea.l 4(a6),a1                    ; color map
  movea.l 8(a6),a2                    ; packed depth LUT
  movea.l 20(a6),a3                   ; advance x table
  movea.l 24(a6),a4                   ; advance y table
  movea.l 12(a6),a5                   ; stop index table
  move.l  28(a6),d0                   ; sample x phase
  move.l  32(a6),d1                   ; sample y phase
  moveq   #0,d2                       ; sample index
  move.l  48(a6),d3                   ; visible top starts at target height
  move.l  d3,d7
  add.l   d7,d7
  moveq   #0,d4
  move.w  0(a5,d7.l),d4               ; current sample limit

.ray_loop:
  cmp.l   d4,d2
  bcc     .ray_done
  tst.l   d3
  beq     .ray_done

  move.l  d1,d5
  lsr.l   #8,d5
  lsr.l   #4,d5
  andi.l  #$000ffc00,d5
  move.l  d0,d7
  lsr.l   #8,d7
  lsr.l   #8,d7
  lsr.l   #6,d7
  or.l    d7,d5                       ; map offset

  move.l  (a2)+,d6                    ; packed projection/step sample
  move.l  d6,d7
  andi.l  #31,d7
  lsl.l   #2,d7
  add.l   0(a3,d7.l),d0
  add.l   0(a4,d7.l),d1

  moveq   #0,d7
  move.b  0(a0,d5.l),d7
  neg.l   d7
  add.l   36(a6),d7                   ; camera height - map height
  lsr.l   #5,d6
  muls.l  d6,d7
  add.l   40(a6),d7                   ; projected height, fixed point
  tst.l   d7
  ble     .height_zero
  asr.l   #8,d7
  asr.l   #5,d7
  cmp.l   48(a6),d7
  ble     .height_ready
  move.l  48(a6),d7
  bra     .height_ready

.height_zero:
  moveq   #0,d7

.height_ready:
  cmp.l   d3,d7
  bge     .sample_done

  move.l  d3,d6                       ; old visible top, before update
  move.l  d7,d3                       ; new visible top
  move.b  0(a1,d5.l),d5
  andi.l  #255,d5                     ; color index

  move.l  d3,d7
  add.l   d7,d7
  moveq   #0,d4
  move.w  0(a5,d7.l),d4               ; tightened sample limit

  move.l  d3,d7
  add.l   44(a6),d7                   ; unclipped span top
  add.l   44(a6),d6                   ; unclipped span bottom
  tst.l   d7
  bge     .top_clipped
  moveq   #0,d7

.top_clipped:
  cmp.l   48(a6),d6
  ble     .bottom_clipped
  move.l  48(a6),d6

.bottom_clipped:
  sub.l   d7,d6                       ; rows
  ble     .sample_done

  move.l  d2,0(sp)                    ; save sample index during fill
  movea.l 4(sp),a0
  addq.l  #1,8(a0)                    ; stats->spans++
  move.l  d6,d2
  muls.l  52(a6),d2
  add.l   d2,12(a0)                   ; stats->pixels += rows * width
  move.l  d6,d2                       ; d2 = row count for fill

  move.l  52(a6),8(sp)                ; remaining columns in span group
  movea.l 16(a6),a1
  adda.l  d7,a1                       ; destination = column base + top

.fill_column_loop:
  move.l  d2,d7                       ; bytes remaining in this column
  move.l  a1,d6
  andi.l  #7,d6
  beq     .fill_aligned
  neg.l   d6
  andi.l  #7,d6                       ; scalar bytes until 8-byte aligned
  cmp.l   d7,d6
  bls     .fill_prefix_ready
  move.l  d7,d6

.fill_prefix_ready:
  sub.l   d6,d7
  tst.l   d6
  beq     .fill_aligned

.fill_prefix_loop:
  move.b  d5,(a1)+
  subq.l  #1,d6
  bne     .fill_prefix_loop

.fill_aligned:
  move.l  d7,d6
  lsr.l   #3,d6
  beq     .fill_tail
  vperm   #$77777777,d5,d5,e0

.fill_block_loop:
  store   e0,(a1)
  addq.l  #8,a1
  subq.l  #1,d6
  bne     .fill_block_loop

.fill_tail:
  andi.l  #7,d7
  beq     .fill_column_done

.fill_tail_loop:
  move.b  d5,(a1)+
  subq.l  #1,d7
  bne     .fill_tail_loop

.fill_column_done:
  move.l  48(a6),d6
  sub.l   d2,d6
  adda.l  d6,a1                       ; next scratch column, same top
  subq.l  #1,8(sp)
  bne     .fill_column_loop

  movea.l 0(a6),a0                    ; restore map pointers
  movea.l 4(a6),a1
  move.l  0(sp),d2                    ; restore sample index

.sample_done:
  addq.l  #1,d2
  bra     .ray_loop

.ray_done:
  movea.l 4(sp),a0
  add.l   d2,4(a0)                    ; stats->depth_samples += samples
  lea     12(sp),sp
  movem.l (sp)+,d2-d7/a2-a6
  rts
