; =====================================================================
; sprites: lasers, enemy bullets, particles, rectangles, reticle
; =====================================================================

; project a world point.  SI -> 3 floats (x, y, z), z must be > 0
; -> AX = sx, DX = sy, [pk] = FX/z (screen pixels per world unit)
proj_point:
        fld dword [c_fx]
        fdiv dword [si+8]
        fst dword [pk]
        fld dword [si]
        fsub dword [camx]
        fmul st0, st1
        fadd dword [c_cx]
        fistp word [tmpw]
        fld dword [si+4]
        fsub dword [camy]
        fmul st0, st1
        fmul dword [c_asp]
        fsubr dword [c_cy]
        fistp word [tmpw2]
        fstp st0
        fwait
        mov ax, [tmpw]
        mov dx, [tmpw2]
        ret

; CF=1 if (ax, dx) is not inside [m, 319-m] x [m, 199-m],  m = BX
on_screen:
        cmp ax, bx
        jl .no
        cmp dx, bx
        jl .no
        push ax
        add ax, bx
        cmp ax, SCR_W - 1
        pop ax
        jg .no
        push dx
        add dx, bx
        cmp dx, SCR_H - 1
        pop dx
        jg .no
        clc
        ret
.no:    stc
        ret

; ---------------------------------------------------------------------
; laser bolt: a tapered streak from its position back along its velocity
; ---------------------------------------------------------------------
render_bolt:
        mov ax, [si+E_IZ]
        cmp ax, 60
        jge .b1
        ret
.b1:    cmp ax, FAR_I
        jle .b2
        ret
.b2:    push si
        ; tail point = pos - vel * 0.03
        fld dword [si+E_VX]
        fmul dword [c_tail]
        fsubr dword [si+E_X]
        fstp dword [pt3]
        fld dword [si+E_VY]
        fmul dword [c_tail]
        fsubr dword [si+E_Y]
        fstp dword [pt3+4]
        fld dword [si+E_VZ]
        fmul dword [c_tail]
        fsubr dword [si+E_Z]
        fstp dword [pt3+8]
        add si, E_X
        call proj_point         ; head
        mov [px], ax
        mov [py], dx
        mov si, pt3
        call proj_point         ; tail
        mov [px+4], ax
        mov [py+4], dx
        pop si
        mov bx, 3
        mov ax, [px]
        mov dx, [py]
        call on_screen
        jc .x
        mov ax, [px+4]
        mov dx, [py+4]
        call on_screen
        jc .x
        ; quad: head 2 pixels wide, tail 4
        mov ax, [px]
        mov [px+2], ax
        dec word [px]
        mov ax, [py]
        mov [py+2], ax
        mov ax, [px+4]
        mov [px+6], ax
        add word [px+4], 1
        sub word [px+6], 2
        mov ax, [py+4]
        mov [py+6], ax
        mov word [pn], 4
        mov ax, [ramp_tab + (RAMP_FLASH*RAMP_LEVELS + RAMP_LEVELS-1)*2]
        mov [curpat], ax
        jmp raster
.x:     ret

; ---------------------------------------------------------------------
; enemy bullet: flashing diamond, size by distance
; ---------------------------------------------------------------------
render_ebul:
        mov ax, [si+E_IZ]
        cmp ax, 30
        jge .b1
        ret
.b1:    cmp ax, FAR_I
        jle .b2
        ret
.b2:    push si
        add si, E_X
        call proj_point
        pop si
        mov [px], ax
        mov [py], dx
        fld dword [pk]
        fmul dword [c_bulsz]
        fistp word [tmpw]
        fwait
        mov bx, [tmpw]
        cmp bx, 1
        jge .s1
        mov bx, 1
.s1:    cmp bx, 6
        jle .s2
        mov bx, 6
.s2:    call on_screen
        jc .x
        ; diamond: top, right, bottom, left
        mov [px+2], ax
        add [px+2], bx
        mov [px+4], ax
        mov [px+6], ax
        sub [px+6], bx
        mov [py+2], dx
        mov [py+6], dx
        mov [py+4], dx
        add [py+4], bx
        sub [py], bx
        mov word [pn], 4
        mov ax, [ramp_tab + (RAMP_FLASH*RAMP_LEVELS + RAMP_LEVELS-1)*2]
        test byte [ticks], 8
        jz .w
        mov ax, [ramp_tab + (RAMP_MAGENTA*RAMP_LEVELS + 2)*2]
.w:     mov [curpat], ax
        jmp raster
.x:     ret

; ---------------------------------------------------------------------
; filled rectangle.  ax=x0 bx=y0 cx=x1 dx=y1 (inclusive), [rectpat]
; ES = back buffer.  Clips to the screen.  Clobbers si.
; ---------------------------------------------------------------------
fill_rect:
        cmp ax, 0
        jge .a
        xor ax, ax
.a:     cmp bx, 0
        jge .b
        xor bx, bx
.b:     cmp cx, SCR_W - 1
        jle .c
        mov cx, SCR_W - 1
.c:     cmp dx, SCR_H - 1
        jle .d
        mov dx, SCR_H - 1
.d:     cmp ax, cx
        jg .x
        cmp bx, dx
        jg .x
        mov [fr_x0], ax
        mov [fr_x1], cx
        mov [fr_y1], dx
        mov si, bx
.r:     mov bx, si
        shl bx, 1
        mov di, [rowofs+bx]
        mov dl, [rectpat]
        test si, 1
        jz .e
        mov dl, [rectpat+1]
.e:     mov ax, [fr_x0]
        mov cx, [fr_x1]
        call span
        inc si
        cmp si, [fr_y1]
        jle .r
.x:     ret

; ---------------------------------------------------------------------
; particles
; ---------------------------------------------------------------------
; spawn CX particles at entity SI's position, colour pattern DX
spawn_particles:
        push si
        push di
        push bx
        mov di, parts
        mov bx, NPART
.l:     test cx, cx
        jz .x
        cmp word [di+P_LIFE], 0
        jg .n
        ; copy position
        mov ax, [si+E_X]
        mov [di+P_X], ax
        mov ax, [si+E_X+2]
        mov [di+P_X+2], ax
        mov ax, [si+E_Y]
        mov [di+P_Y], ax
        mov ax, [si+E_Y+2]
        mov [di+P_Y+2], ax
        mov ax, [si+E_Z]
        mov [di+P_Z], ax
        mov ax, [si+E_Z+2]
        mov [di+P_Z+2], ax
        push cx
        mov cx, 300
        call rand_mod
        sub ax, 150
        SETF [di+P_VX], ax
        mov cx, 260
        call rand_mod
        sub ax, 70
        SETF [di+P_VY], ax
        mov cx, 300
        call rand_mod
        sub ax, 300
        SETF [di+P_VZ], ax
        mov cx, 50
        call rand_mod
        add ax, 45
        mov [di+P_LIFE], ax
        mov [di+P_PAT], dx
        pop cx
        dec cx
.n:     add di, P_SIZE
        dec bx
        jnz .l
.x:     fwait
        pop bx
        pop di
        pop si
        ret

update_particles:
        fld dword [c_gravity]
        fmul dword [dtf]
        fstp dword [gdt]
        mov si, parts
        mov cx, NPART
.l:     mov ax, [si+P_LIFE]
        test ax, ax
        jle .n
        sub ax, [dtt]
        jg .alive
        xor ax, ax
.alive: mov [si+P_LIFE], ax
        jle .n
        fld dword [si+P_VY]
        fsub dword [gdt]
        fst dword [si+P_VY]
        fmul dword [dtf]
        fadd dword [si+P_Y]
        fstp dword [si+P_Y]
        fld dword [si+P_VX]
        fmul dword [dtf]
        fadd dword [si+P_X]
        fstp dword [si+P_X]
        fld dword [si+P_VZ]
        fmul dword [dtf]
        fadd dword [si+P_Z]
        fstp dword [si+P_Z]
.n:     add si, P_SIZE
        dec cx
        jnz .l
        fwait
        ret

render_particles:
        push es
        mov es, [backseg]
        mov si, parts
        mov cx, NPART
.l:     cmp word [si+P_LIFE], 0
        jle .n
        fld dword [si+P_Z]
        fistp word [tmpw]
        fwait
        cmp word [tmpw], 30
        jl .n
        cmp word [tmpw], FAR_I
        jg .n
        push cx
        push si
        call proj_point
        mov [fr_sx], ax
        mov [fr_sy], dx
        fld dword [pk]
        fmul dword [c_partsz]
        fistp word [tmpw]
        fwait
        pop si
        push si
        ; young particles are white-hot, then take their own colour
        mov ax, [ramp_tab + (RAMP_FLASH*RAMP_LEVELS + RAMP_LEVELS-1)*2]
        cmp word [si+P_LIFE], 40
        jg .hot
        mov ax, [si+P_PAT]
.hot:   mov [rectpat], ax
        mov bx, [tmpw]
        cmp bx, 3
        jle .s
        mov bx, 3
.s:     mov ax, [fr_sx]
        mov cx, ax
        add cx, bx
        mov dx, [fr_sy]
        add dx, bx
        mov bx, [fr_sy]
        cmp ax, -4              ; far off screen: fill_rect clamps, but
        jl .skip                ; skip obviously invisible ones
        cmp bx, -4
        jl .skip
        call fill_rect
.skip:  pop si
        pop cx
.n:     add si, P_SIZE
        dec cx
        jnz .l
        pop es
        ret

; ---------------------------------------------------------------------
; aiming reticle: where the lasers will be in ~0.32 s
; ---------------------------------------------------------------------
draw_reticle:
        fld dword [svx]
        fmul dword [c_aimt]
        fadd dword [ents+E_X]
        fstp dword [pt3]
        fld dword [svy]
        fmul dword [c_aimt]
        fadd dword [ents+E_Y]
        fsub dword [c_2]
        fstp dword [pt3+4]
        fld dword [c_retz]
        fstp dword [pt3+8]
        mov si, pt3
        call proj_point
        mov bx, 8
        call on_screen
        jc .x
        mov [fr_sx], ax
        mov [fr_sy], dx
        mov word [rectpat], 0FFFFh
        ; four corner brackets
        mov si, ret_parts
        mov bp, 8
.l:     lodsw
        add ax, [fr_sx]
        push ax
        lodsw
        add ax, [fr_sy]
        mov bx, ax
        lodsw
        add ax, [fr_sx]
        mov cx, ax
        lodsw
        add ax, [fr_sy]
        mov dx, ax
        pop ax
        push si
        push bp
        call fill_rect
        pop bp
        pop si
        dec bp
        jnz .l
.x:     ret

ret_parts:      ; x0, y0, x1, y1 relative to the centre
        dw -7, -5, -4, -5,   -7, -5, -7, -3
        dw  4, -5,  7, -5,    7, -5,  7, -3
        dw -7,  5, -4,  5,   -7,  3, -7,  5
        dw  4,  5,  7,  5,    7,  3,  7,  5
