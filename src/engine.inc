; =====================================================================
; 3D engine: background, entities, transform, clipping, rasterizer
; =====================================================================

; ---------------------------------------------------------------------
; sky + scrolling striped ground.
; Ground stripes are world-space bands of depth SP.  Instead of a divide
; per scanline, only the ~12 rows where a stripe boundary lands are
; computed:  y = horizon + H*FY / z.  While the 8087 grinds through
; that FDIV (~200 clocks) the 8088 is filling the rows of the stripe
; above it with REP STOSW - the two chips work in parallel.
; ---------------------------------------------------------------------
render_bg:
        push es
        mov es, [backseg]
        xor di, di
        xor bp, bp              ; y
        mov dx, PAT_SKY
        mov bx, HORIZON - 6
        call fill_rows
        mov dx, PAT_HAZE2
        mov bx, 3
        call fill_rows
        mov dx, PAT_HAZE1
        mov bx, 4
        call fill_rows          ; rows up to and including the horizon

        fld dword [camy]
        fsub dword [c_ground]
        fmul dword [c_fy]
        fstp dword [hfy]        ; camera height * FY

        ; nearest stripe boundary inside the fog distance:
        ; m = round((FOG + scroll) / SP),  z = m*SP - scroll
        fld dword [c_fogz]
        fadd dword [scroll]
        fmul dword [c_invsp]
        frndint
        fist word [gm]
        fmul dword [c_sp]
        fsub dword [scroll]
        fcom dword [c_fogz]
        FFLAGS
        jbe .zok
        fsub dword [c_sp]
        dec word [gm]
.zok:   fld dword [hfy]         ; first boundary row (synchronous)
        fdiv st0, st1
        fistp word [gy]
        fwait
        call ground_row
        mov dx, PAT_FOG         ; beyond the fog line
.gl:    fsub dword [c_sp]       ; next boundary, one stripe closer
        fld dword [hfy]
        fdiv st0, st1           ; 8087 starts dividing...
        mov bx, [cury]          ; ...8088 fills the current stripe
        call fill_to
        fistp word [gy]
        fwait
        dec word [gm]           ; stripe below boundary m is band m-1
        mov dx, PAT_GROUND_A
        test byte [gm], 1
        jz .pa
        mov dx, PAT_GROUND_B
.pa:    call ground_row
        cmp bp, SCR_H
        jb .gl
        fstp st0
        fwait
        pop es
        ret

; gy (row of a stripe boundary, may be garbage) -> cury in [bp, 200]
ground_row:
        mov ax, [gy]
        cmp ax, SCR_H - HORIZON - 1
        jbe .in                 ; unsigned: also rejects negatives
        mov ax, SCR_H
        jmp short .st
.in:    add ax, HORIZON
        cmp ax, bp
        jae .st
        mov ax, bp
.st:    mov [cury], ax
        ret

; fill rows bp .. bx-1 with pattern dx
fill_to:
        cmp bp, bx
        jae .x
        sub bx, bp
        jmp short fill_rows
.x:     ret

; dx = pattern, bx = rows; fills from di/bp onward
fill_rows:
.r:     mov al, dl
        test bp, 1
        jz .e
        mov al, dh
.e:     mov ah, al
        mov cx, 40
        rep stosw
        inc bp
        dec bx
        jnz .r
        ret

; ---------------------------------------------------------------------
; render one entity.  SI = entity
; ---------------------------------------------------------------------
render_entity:
        mov ax, [si+E_KIND]
        cmp ax, K_BOLT
        jne .nb
        jmp render_bolt
.nb:    cmp ax, K_EBUL
        jne .solid
        jmp render_ebul
.solid: mov bx, [si+E_MODEL]
        mov [cur_model], bx
        ; --- integer bounding-sphere culling
        mov ax, [si+E_IZ]
        mov dx, [bx+M_RAD]
        mov cx, ax
        add cx, dx
        cmp cx, NEAR_I
        jge .c1
        ret
.c1:    mov cx, ax
        sub cx, dx
        cmp cx, FAR_I
        jle .c2
        ret
.c2:    mov cx, [si+E_IX]
        sub cx, [icamx]
        jns .c3
        neg cx
.c3:    sub cx, dx
        jle .c4
        mov dx, ax              ; outside the side planes (|x| > 0.875 z)?
        sar dx, 1
        sar dx, 1
        sar dx, 1
        sub ax, dx
        cmp cx, ax
        jle .c4
        ret
.c4:
        ; --- translation (camera space)
        fld dword [si+E_X]
        fsub dword [camx]
        fstp dword [tx]
        fld dword [si+E_Y]
        fsub dword [camy]
        fstp dword [ty]
        fld dword [si+E_Z]
        fstp dword [tz]
        mov ax, [si+E_RAMP]     ; hit or exploding: flash white
        cmp word [si+E_HIT], 0
        jne .flash
        cmp word [si+E_STATE], 0
        je .nfl
.flash: mov ax, RAMP_FLASH
.nfl:   mov [cur_ramp], ax
        call build_matrix
        call object_space_eye_light
        call classify_faces
        cmp word [nvis], 0
        jne .vis
        ret
.vis:   call transform_verts
        mov bx, [cur_model]
        test word [bx+M_FLAGS], MF_SORT
        jz .draw
        call sort_faces
.draw:  xor bx, bx
.dl:    cmp bx, [nvis]
        jae .done
        push bx
        add bx, vislist
        call draw_face
        pop bx
        add bx, 6
        jmp short .dl
.done:  ret

; R = Ry(yaw) * Rx(pitch) * Rz(roll), from the float sine table
build_matrix:
        push si
        push di
        mov ax, [si+E_ROLL]
        call angptr
        push bx
        mov ax, [si+E_PITCH]
        call angptr
        mov di, bx
        mov ax, [si+E_YAW]
        call angptr
        mov si, bx              ; si: yaw  di: pitch  bx: roll
        pop bx                  ; sin at +0, cos at +256
        fld dword [di]
        fmul dword [bx]         ; t1 = sp*sr
        fld dword [di]
        fmul dword [bx+256]     ; t2 = sp*cr      st: t2 t1
        ; m00 = cy*cr + sy*t1
        fld dword [si+256]
        fmul dword [bx+256]
        fld dword [si]
        fmul st0, st3
        faddp st1, st0
        fstp dword [mat+0]
        ; m01 = sy*t2 - cy*sr
        fld dword [si]
        fmul st0, st1
        fld dword [si+256]
        fmul dword [bx]
        fsubp st1, st0
        fstp dword [mat+4]
        ; m02 = sy*cp
        fld dword [si]
        fmul dword [di+256]
        fstp dword [mat+8]
        ; m10 = cp*sr  m11 = cp*cr  m12 = -sp
        fld dword [di+256]
        fmul dword [bx]
        fstp dword [mat+12]
        fld dword [di+256]
        fmul dword [bx+256]
        fstp dword [mat+16]
        fld dword [di]
        fchs
        fstp dword [mat+20]
        ; m20 = cy*t1 - sy*cr
        fld dword [si+256]
        fmul st0, st2
        fld dword [si]
        fmul dword [bx+256]
        fsubp st1, st0
        fstp dword [mat+24]
        ; m21 = sy*sr + cy*t2
        fld dword [si]
        fmul dword [bx]
        fld dword [si+256]
        fmul st0, st2
        faddp st1, st0
        fstp dword [mat+28]
        ; m22 = cy*cp
        fld dword [si+256]
        fmul dword [di+256]
        fstp dword [mat+32]
        fstp st0
        fstp st0
        pop di
        pop si
        ret

; e = R^T * (-t)  (camera position in model space), l = R^T * L
; column dot product: [bx] . (a,b,c) with column k of R
%macro COLDOT 4                 ; col offset, a, b, c
        fld dword [mat+%1]
        fmul dword [%2]
        fld dword [mat+%1+12]
        fmul dword [%3]
        faddp st1, st0
        fld dword [mat+%1+24]
        fmul dword [%4]
        faddp st1, st0
%endmacro
object_space_eye_light:
        COLDOT 0, tx, ty, tz
        fchs
        fstp dword [eye]
        COLDOT 4, tx, ty, tz
        fchs
        fstp dword [eye+4]
        COLDOT 8, tx, ty, tz
        fchs
        fstp dword [eye+8]
        COLDOT 0, c_lx, c_ly, c_lz
        fstp dword [lgt]
        COLDOT 4, c_lx, c_ly, c_lz
        fstp dword [lgt+4]
        COLDOT 8, c_lx, c_ly, c_lz
        fstp dword [lgt+8]
        ret

; back-face cull + flat shading, fills vislist
%macro FDOT3 2                  ; face ptr reg, vec
        fld dword [%1+F_NX]
        fmul dword [%2]
        fld dword [%1+F_NY]
        fmul dword [%2+4]
        faddp st1, st0
        fld dword [%1+F_NZ]
        fmul dword [%2+8]
        faddp st1, st0
%endmacro
classify_faces:
        mov bx, [cur_model]
        mov cx, [bx+M_NV]       ; clear the "vertex needed" flags
        shl cx, 1               ; (one dword per vertex)
        mov di, vneed
        xor ax, ax
        rep stosw
        mov cx, [bx+M_NF]
        mov di, [bx+M_FACES]
        xor bx, bx              ; vislist offset
.fl:    FDOT3 di, eye
        fsub dword [di+F_D]
        fstp dword [ftmp]
        fwait
        test byte [ftmp+3], 80h ; facing away?
        jnz .next
        FDOT3 di, lgt
        fmul dword [c_shk]
        fadd dword [c_shb]      ; 8087 busy: 8088 marks this face's vertices
        push cx
        push si
        xor ch, ch
        mov cl, [di+F_NV]
        lea si, [di+F_IDX]
.mk:    lodsb
        xor ah, ah
        shl ax, 1
        shl ax, 1
        mov bp, ax
        mov byte [vneed+bp], 1
        loop .mk
        pop si
        pop cx
        fistp word [lev]
        fwait
        mov ax, [lev]
        test ax, ax
        jge .l1
        xor ax, ax
.l1:    cmp ax, RAMP_LEVELS - 1
        jle .l2
        mov ax, RAMP_LEVELS - 1
.l2:    mov dx, [cur_ramp]
        cmp dl, 0FFh
        jne .r
        mov dl, [di+F_RAMP]
.r:     xor dh, dh
        push ax
        mov ax, dx
        mov dx, RAMP_LEVELS
        mul dx
        pop dx
        add ax, dx
        shl ax, 1
        add ax, ramp_tab
        mov [vislist+bx], di
        mov [vislist+bx+2], ax
        add bx, 6
        cmp bx, MAXVIS * 6
        jae .full
.next:  add di, FACE_SIZE
        dec cx
        jz .fl_end
        jmp .fl
.fl_end:
.full:  mov ax, bx
        mov [nvis], ax
        ret

; ---------------------------------------------------------------------
; transform + project all vertices of the current model.
; Pipelined: the 8088 classifies vertex i-1 against the screen while
; the 8087 performs the perspective FDIV for vertex i.
; ---------------------------------------------------------------------
%macro ROWDOT 1                 ; matrix row offset; st: x y z
        fld dword [mat+%1]
        fmul st0, st1
        fld dword [mat+%1+4]
        fmul st0, st3
        faddp st1, st0
        fld dword [mat+%1+8]
        fmul st0, st4
        faddp st1, st0
%endmacro
transform_verts:
        push si
        mov bx, [cur_model]
        mov cx, [bx+M_NV]
        lea si, [bx+M_VERTS]
        xor di, di
        mov bp, -1
.vl:    cmp byte [vneed+di], 0  ; vertex only on back faces: skip it
        jne .need
        jmp .nv
.need:  fild word [si+4]
        fild word [si+2]
        fild word [si]
        ROWDOT 0
        fadd dword [tx]
        fstp dword [vfx+di]
        ROWDOT 12
        fadd dword [ty]
        fstp dword [vfy+di]
        ROWDOT 24
        fadd dword [tz]
        fstp dword [vfz+di]
        fstp st0
        fstp st0
        fstp st0
        fwait
        cmp word [vfz+di+2], NEAR_HI    ; float compare via its bits
        jge .front
        mov byte [vcode+di], 1  ; behind the near plane
        jmp .nv
.front: fld dword [c_fx]
        fdiv dword [vfz+di]     ; 8087: ~200 clocks...
        cmp bp, -1              ; ...8088: finish the previous vertex
        je .np
        call finalize_vertex
.np:    mov bp, di
        fld st0
        fmul dword [vfx+di]
        fadd dword [c_cx]
        fistp dword [sxd+di]
        fmul dword [c_asp]
        fmul dword [vfy+di]
        fsubr dword [c_cy]
        fistp dword [syd+di]
.nv:    add si, 6
        add di, 4
        dec cx
        jz .vl_end
        jmp .vl
.vl_end:
        fwait
        cmp bp, -1
        je .x
        call finalize_vertex
.x:     pop si
        ret

; outcode for vertex BP (32-bit screen coords in sxd/syd)
finalize_vertex:
        xor bl, bl
        mov ax, [sxd+bp]
        mov dx, [sxd+bp+2]
        test dx, dx
        js .left
        jnz .right
        cmp ax, SCR_W - 1
        jbe .yc
.right: or bl, 4
        jmp short .yc
.left:  or bl, 2
.yc:    mov ax, [syd+bp]
        mov dx, [syd+bp+2]
        test dx, dx
        js .top
        jnz .bot
        cmp ax, SCR_H - 1
        jbe .done
.bot:   or bl, 16
        jmp short .done
.top:   or bl, 8
.done:  mov [vcode+bp], bl
        ret

; average depth of each visible face, then insertion sort (far first)
sort_faces:
        xor bx, bx
.d:     cmp bx, [nvis]
        jae .sort
        mov di, [vislist+bx]
        xor ch, ch
        mov cl, [di+F_NV]
        fldz
        lea si, [di+F_IDX]
.s:     lodsb
        xor ah, ah
        shl ax, 1
        shl ax, 1
        mov bp, ax
        fadd dword [vfz+bp]
        loop .s
        mov al, [di+F_NV]
        xor ah, ah
        shl ax, 1
        shl ax, 1
        mov bp, ax
        fmul dword [c_inv+bp]
        fistp word [vislist+bx+4]
        add bx, 6
        jmp short .d
.sort:  fwait
        mov bx, 6
.o:     cmp bx, [nvis]
        jae .x
        mov ax, [vislist+bx]
        mov cx, [vislist+bx+2]
        mov dx, [vislist+bx+4]
        mov di, bx
.i:     test di, di
        jz .p
        cmp dx, [vislist+di-2]
        jle .p
        mov si, [vislist+di-6]
        mov [vislist+di], si
        mov si, [vislist+di-4]
        mov [vislist+di+2], si
        mov si, [vislist+di-2]
        mov [vislist+di+4], si
        sub di, 6
        jmp short .i
.p:     mov [vislist+di], ax
        mov [vislist+di+2], cx
        mov [vislist+di+4], dx
        add bx, 6
        jmp short .o
.x:     ret

; ---------------------------------------------------------------------
; draw one visible face.  BX -> vislist entry
; ---------------------------------------------------------------------
draw_face:
        mov di, [bx]
        mov si, [bx+2]
        mov ax, [si]
        mov [curpat], ax
        xor ch, ch
        mov cl, [di+F_NV]
        mov [pn], cx
        lea si, [di+F_IDX]
        mov dx, 00FFh           ; dl = AND of outcodes, dh = OR
.oc:    lodsb
        xor ah, ah
        shl ax, 1
        shl ax, 1
        mov bp, ax
        mov al, [vcode+bp]
        and dl, al
        or dh, al
        loop .oc
        test dl, dl
        jz .vis
        ret                     ; trivially outside
.vis:   test dh, dh
        jnz .clip
        ; trivially inside: projected vertices go straight to the rasterizer
        mov cx, [pn]
        lea si, [di+F_IDX]
        xor bx, bx
.cp:    lodsb
        xor ah, ah
        shl ax, 1
        shl ax, 1
        mov bp, ax
        mov ax, [sxd+bp]
        mov [px+bx], ax
        mov ax, [syd+bp]
        mov [py+bx], ax
        add bx, 2
        loop .cp
        jmp raster

        ; --- needs clipping: copy camera-space vertices to the clip buffer
.clip:  mov [orcode], dh
        mov cx, [pn]
        lea si, [di+F_IDX]
        mov bx, clipa
.cc:    lodsb
        xor ah, ah
        shl ax, 1
        shl ax, 1
        mov bp, ax
        mov ax, [vfx+bp]
        mov [bx], ax
        mov ax, [vfx+bp+2]
        mov [bx+2], ax
        mov ax, [vfy+bp]
        mov [bx+4], ax
        mov ax, [vfy+bp+2]
        mov [bx+6], ax
        mov ax, [vfz+bp]
        mov [bx+8], ax
        mov ax, [vfz+bp+2]
        mov [bx+10], ax
        add bx, 12
        loop .cc
        mov word [csrc], clipa
        mov word [cdst], clipb
        ; near plane forces all planes (outcodes of near vertices are unknown)
        mov al, [orcode]
        test al, 1
        jz .pl
        mov al, 1Fh
.pl:    mov [orcode], al
        mov bx, clip_planes
        mov ah, 1
.pp:    test [orcode], ah
        jz .pn
        push ax
        push bx
        call clip_plane
        pop bx
        pop ax
        cmp word [pn], 3
        jae .pn
        ret
.pn:    add bx, 16
        shl ah, 1
        cmp ah, 20h
        jb .pp
        ; project the clipped polygon
        mov si, [csrc]
        mov cx, [pn]
        xor bx, bx
.pr:    fld dword [c_fx]
        fdiv dword [si+8]
        fld st0
        fmul dword [si]
        fadd dword [c_cx]
        fistp word [px+bx]
        fmul dword [c_asp]
        fmul dword [si+4]
        fsubr dword [c_cy]
        fistp word [py+bx]
        fwait
        mov ax, [px+bx]         ; clamp rounding spill
        test ax, ax
        jge .x1
        xor ax, ax
.x1:    cmp ax, SCR_W - 1
        jle .x2
        mov ax, SCR_W - 1
.x2:    mov [px+bx], ax
        mov ax, [py+bx]
        test ax, ax
        jge .y1
        xor ax, ax
.y1:    cmp ax, SCR_H - 1
        jle .y2
        mov ax, SCR_H - 1
.y2:    mov [py+bx], ax
        add si, 12
        add bx, 2
        dec cx
        jz .pr_end
        jmp .pr
.pr_end:
        jmp raster

; Sutherland-Hodgman against one plane (BX -> a,b,c,d floats)
; csrc/pn -> cdst, then swaps buffers
clip_plane:
        mov si, [csrc]
        mov cx, [pn]
        xor di, di
.dist:  fld dword [bx]
        fmul dword [si]
        fld dword [bx+4]
        fmul dword [si+4]
        faddp st1, st0
        fld dword [bx+8]
        fmul dword [si+8]
        faddp st1, st0
        fadd dword [bx+12]
        fstp dword [cdist+di]
        add si, 12
        add di, 4
        loop .dist
        fwait
        mov si, [csrc]          ; current vertex
        mov di, [cdst]          ; output
        xor bp, bp              ; current index*4
        xor dx, dx              ; output count
        mov cx, [pn]
.e:     ; next vertex (wraps)
        mov bx, bp
        add bx, 4
        mov ax, [pn]
        shl ax, 1
        shl ax, 1
        cmp bx, ax
        jb .nw
        xor bx, bx
.nw:    mov al, [cdist+bp+3]
        test al, 80h
        jnz .out
        ; current inside: emit it
        push cx
        push si
        mov cx, 6
        rep movsw
        pop si
        pop cx
        inc dx
.out:   mov ah, [cdist+bx+3]
        xor al, ah
        test al, 80h
        jz .ne                  ; no crossing
        ; t = d0 / (d0 - d1); out = cur + t*(next-cur)
        fld dword [cdist+bp]
        fld st0
        fsub dword [cdist+bx]
        fdivp st1, st0
        push bx
        mov ax, bx              ; next vertex address = csrc + idx*3
        shl bx, 1
        add bx, ax
        add bx, [csrc]
        fld dword [bx]
        fsub dword [si]
        fmul st0, st1
        fadd dword [si]
        fstp dword [di]
        fld dword [bx+4]
        fsub dword [si+4]
        fmul st0, st1
        fadd dword [si+4]
        fstp dword [di+4]
        fld dword [bx+8]
        fsub dword [si+8]
        fmul st0, st1
        fadd dword [si+8]
        fstp dword [di+8]
        fstp st0
        pop bx
        add di, 12
        inc dx
.ne:    add si, 12
        add bp, 4
        dec cx
        jz .e_end
        jmp .e
.e_end:
        fwait
        mov [pn], dx
        mov ax, [csrc]
        xchg ax, [cdst]
        mov [csrc], ax
        ret

; ---------------------------------------------------------------------
; convex polygon rasterizer (pure integer, 8088)
;   px/py/pn -> min/max x per scanline -> dithered spans
; ---------------------------------------------------------------------
raster:
        mov cx, [pn]
        cmp cx, 3
        jae .ok
        ret
.ok:    mov ax, [py]
        mov [ymin], ax
        mov [ymax], ax
        mov bx, 2
        dec cx
.mm:    mov ax, [py+bx]
        cmp ax, [ymin]
        jge .m1
        mov [ymin], ax
.m1:    cmp ax, [ymax]
        jle .m2
        mov [ymax], ax
.m2:    add bx, 2
        loop .mm
        mov di, [ymin]
        shl di, 1
        mov cx, [ymax]
        sub cx, [ymin]
        inc cx
.in:    mov word [xl+di], 7FFFh
        mov word [xr+di], 8000h
        add di, 2
        loop .in
        ; walk all edges
        xor bx, bx
.ed:    mov [ek], bx
        mov ax, [px+bx]
        mov cx, [py+bx]
        mov [e_y0], cx
        add bx, 2
        mov dx, [pn]
        shl dx, 1
        cmp bx, dx
        jb .w
        xor bx, bx
.w:     mov cx, [px+bx]
        mov dx, [py+bx]
        mov bx, [e_y0]
        call edge
        mov bx, [ek]
        add bx, 2
        mov dx, [pn]
        shl dx, 1
        cmp bx, dx
        jb .ed
        ; fill spans
        push es
        mov es, [backseg]
        mov si, [ymin]
.row:   mov bx, si
        shl bx, 1
        mov ax, [xl+bx]
        mov cx, [xr+bx]
        cmp ax, cx
        jg .nr
        mov di, [rowofs+bx]
        mov dl, [curpat]
        test si, 1
        jz .ev
        mov dl, [curpat+1]
.ev:    call span
.nr:    inc si
        cmp si, [ymax]
        jle .row
        pop es
        ret

; edge (ax,bx) -> (cx,dx) into xl/xr
edge:
        cmp bx, dx
        jle .o
        xchg ax, cx
        xchg bx, dx
.o:     sub dx, bx              ; dy
        mov si, bx
        shl si, 1
        mov di, 1
        sub cx, ax              ; dx
        jge .pos
        neg cx
        mov di, -1
.pos:   test dx, dx
        jnz .slope
        call .upd
        cmp di, 0
        jg .fa
        sub ax, cx
        jmp short .upd
.fa:    add ax, cx
.upd:   cmp ax, [xl+si]
        jge .u1
        mov [xl+si], ax
.u1:    cmp ax, [xr+si]
        jle .u2
        mov [xr+si], ax
.u2:    ret
.slope: mov bp, dx
        push ax
        mov ax, cx
        xor dx, dx
        div bp                  ; q = |dx| / dy, r in dx
        cmp di, 0
        jg .qp
        neg ax
.qp:    mov [eq], ax
        pop ax
        mov cx, bp
        inc cx
        mov bx, bp
        shr bx, 1               ; error term starts at dy/2
.lp:    cmp ax, [xl+si]
        jge .a
        mov [xl+si], ax
.a:     cmp ax, [xr+si]
        jle .b
        mov [xr+si], ax
.b:     add si, 2
        add ax, [eq]
        add bx, dx
        cmp bx, bp
        jl .c
        sub bx, bp
        add ax, di
.c:     loop .lp
        ret

; span: ax = x0, cx = x1 (inclusive), dl = pattern byte, di = row offset
span:
        mov bx, ax
        and bx, 3
        mov dh, [lmask+bx]
        mov bx, cx
        and bx, 3
        mov bl, [rmask+bx]
        shr ax, 1
        shr ax, 1
        shr cx, 1
        shr cx, 1
        add di, ax
        sub cx, ax
        jnz .multi
        and dh, bl
        mov al, [es:di]
        mov ah, al
        xor ah, dl
        and ah, dh
        xor al, ah
        mov [es:di], al
        ret
.multi: mov al, [es:di]
        mov ah, al
        xor ah, dl
        and ah, dh
        xor al, ah
        stosb
        dec cx
        mov al, dl
        mov ah, dl
        shr cx, 1
        rep stosw
        adc cx, cx
        rep stosb
        mov al, [es:di]
        mov ah, al
        xor ah, dl
        and ah, bl
        xor al, ah
        mov [es:di], al
        ret

lmask:  db 0FFh, 3Fh, 0Fh, 03h
rmask:  db 0C0h, 0F0h, 0FCh, 0FFh

; ---------------------------------------------------------------------
; copy the linear back buffer to interleaved CGA memory
; ---------------------------------------------------------------------
blit:
        push ds
        push es
        mov ax, 0B800h
        mov es, ax
        mov ax, [backseg]
        mov ds, ax
        xor si, si
        xor di, di
        mov dx, 100
.l:     mov cx, 40
        rep movsw
        add di, 2000h - 80
        mov cx, 40
        rep movsw
        sub di, 2000h
        dec dx
        jnz .l
        pop es
        pop ds
        ret

