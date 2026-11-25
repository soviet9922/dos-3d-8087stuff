; =====================================================================
; text and HUD (ROM 8x8 font at F000:FA6E, chars 0..127)
; =====================================================================

TXT_WHITE   equ 0FFFFh
TXT_MAGENTA equ 0AAAAh
TXT_CYAN    equ 05555h

; si = asciiz string, di = backbuffer offset, [tcolor] = colour word
draw_text:
.l:     lodsb
        test al, al
        jz .x
        call draw_char
        add di, 2
        jmp .l
.x:     ret

; glyph pixels are replaced by the text colour, with a drop shadow
draw_char:
        push si
        push di
        push cx
        push bx
        push ds
        xor ah, ah
        mov si, ax
        mov cl, 3
        shl si, cl
        add si, 0FA6Eh
        mov cx, 0F000h
        mov ds, cx
        mov cx, 8
.r:     lodsb
        xor ah, ah
        mov bx, ax
        shl bx, 1
        mov bx, [cs:expand+bx]
        not bx
        and [es:di+80], bx      ; shadow
        and [es:di], bx
        not bx
        and bx, [cs:tcolor]
        or [es:di], bx
        add di, 80
        loop .r
        pop ds
        pop bx
        pop cx
        pop di
        pop si
        ret

; double size (16x16)
draw_text_big:
.l:     lodsb
        test al, al
        jz .x
        call draw_char_big
        add di, 4
        jmp .l
.x:     ret

draw_char_big:
        push si
        push di
        push cx
        push bx
        push dx
        push ds
        xor ah, ah
        mov si, ax
        mov cl, 3
        shl si, cl
        add si, 0FA6Eh
        mov cx, 0F000h
        mov ds, cx
        mov cx, 8
.r:     lodsb
        xor ah, ah
        mov bx, ax
        shl bx, 1
        shl bx, 1
        mov dx, [cs:expand2+bx+2]
        mov bx, [cs:expand2+bx]
        push cx
        mov cx, 2               ; every font row twice
.rr:    not bx
        not dx
        and [es:di+80], bx
        and [es:di+82], dx
        and [es:di], bx
        and [es:di+2], dx
        not bx
        not dx
        push bx
        push dx
        and bx, [cs:tcolor]
        and dx, [cs:tcolor]
        or [es:di], bx
        or [es:di+2], dx
        pop dx
        pop bx
        add di, 80
        loop .rr
        pop cx
        loop .r
        pop ds
        pop dx
        pop bx
        pop cx
        pop di
        pop si
        ret

; centred text.  si = string, ax = row; small (ch=0) or big (ch=1)
text_center:
        push ax
        push si
        xor cl, cl
.n:     lodsb
        test al, al
        jz .e
        inc cl
        jmp .n
.e:     pop si
        pop ax
        xor dx, dx
        mov dl, cl
        shl dx, 1               ; bytes per small char = 2
        test ch, ch
        jz .s
        shl dx, 1               ; big = 4
.s:     mov bx, 80
        sub bx, dx
        shr bx, 1
        mov dx, 80
        mul dx
        add ax, bx
        mov di, ax
        test ch, ch
        jnz .big
        jmp draw_text
.big:   jmp draw_text_big

; ax -> 5 decimal digits at numbuf
utoa:
        push di
        push cx
        push bx
        push dx
        mov di, numbuf + 4
        mov cx, 5
        mov bx, 10
.l:     xor dx, dx
        div bx
        add dl, '0'
        mov [di], dl
        dec di
        loop .l
        pop dx
        pop bx
        pop cx
        pop di
        ret

; print number AX (5 digits + trailing '0': scores are kept in tens)
; at di
draw_score:
        call utoa
        mov byte [numbuf+5], '0'
        mov byte [numbuf+6], 0
        mov si, numbuf
        call draw_text
        mov byte [numbuf+5], 0
        ret

; horizontal bar: ax=x, bx=y, cx=width of frame, dx=filled pixels,
; [barpat] = fill pattern
draw_bar:
        mov [bar_x], ax
        mov [bar_y], bx
        mov [bar_w], cx
        mov [bar_f], dx
        mov word [rectpat], 0FFFFh      ; white frame
        add cx, ax
        inc cx
        mov dx, bx
        add dx, 5
        call fill_rect
        mov word [rectpat], 0           ; empty inside
        mov ax, [bar_x]
        inc ax
        mov bx, [bar_y]
        inc bx
        mov cx, ax
        add cx, [bar_w]
        dec cx
        mov dx, bx
        add dx, 3
        call fill_rect
        mov cx, [bar_f]
        jcxz .x
        mov ax, [barpat]
        mov [rectpat], ax
        mov ax, [bar_x]
        inc ax
        mov bx, [bar_y]
        inc bx
        mov cx, ax
        add cx, [bar_f]
        dec cx
        mov dx, bx
        add dx, 3
        call fill_rect
.x:     ret

draw_hud:
        push es
        mov es, [backseg]
        mov word [tcolor], TXT_WHITE
        cmp byte [gstate], GS_TITLE
        jne .game
        call draw_title
        jmp .common

.game:  ; --- score, ships, bombs
        mov si, s_score
        mov di, 2*80 + 1
        call draw_text
        mov ax, [score]
        mov di, 2*80 + 13
        call draw_score
        mov si, s_ships
        mov di, 2*80 + 50
        call draw_text
        mov al, [lives]
        add al, '0'
        mov di, 2*80 + 62
        call draw_char
        mov si, s_bombs
        mov di, 2*80 + 66
        call draw_text
        mov al, [bombs]
        add al, '0'
        mov di, 2*80 + 78
        call draw_char

        ; --- shield bar
        mov si, s_shield
        mov di, 190*80 + 1
        call draw_text
        mov word [barpat], 0FFFFh
        cmp word [shield], 6
        jg .sok
        test byte [ticks], 16           ; low shield: flashing magenta
        jz .sok
        mov word [barpat], 0AAAAh
.sok:   mov ax, 54
        mov bx, 191
        mov cx, SHIELD_MAX * 3
        mov dx, [shield]
        mov si, dx
        shl dx, 1
        add dx, si
        call draw_bar

        ; --- boss bar
        cmp byte [boss_active], 0
        je .nb
        call boss_hp
        mov [tmpw], ax
        mov si, s_boss
        mov di, 13*80 + 18
        call draw_text
        mov ax, [tmpw]
        mov cx, 120
        mul cx
        div word [boss_maxhp]
        mov dx, ax
        mov word [barpat], 0AAAAh
        mov ax, 120
        mov bx, 14
        mov cx, 120
        call draw_bar
.nb:
        ; --- reticle
        cmp byte [gstate], GS_PLAY
        jne .nr
        call draw_reticle
.nr:
        ; --- big state banners
        mov al, [gstate]
        cmp al, GS_OVER
        jne .n1
        mov word [tcolor], TXT_MAGENTA
        mov si, s_gameover
        mov ax, 72
        mov ch, 1
        call text_center
        jmp .common
.n1:    cmp al, GS_CLEAR
        jne .n2
        mov word [tcolor], TXT_CYAN
        mov si, s_clear
        mov ax, 60
        mov ch, 1
        call text_center
        mov word [tcolor], TXT_WHITE
        mov si, s_bonus
        mov di, 90*80 + 26
        call draw_text
        mov ax, [bonus]
        mov di, 90*80 + 40
        call draw_score
        jmp .common
.n2:
.common:
        ; --- message line
        cmp word [msg_time], 0
        je .nm
        test byte [ticks], 32
        jz .nm
        mov word [tcolor], TXT_WHITE
        mov si, [msg_ptr]
        mov ax, 44
        xor ch, ch
        call text_center
.nm:    cmp byte [paused], 0
        je .np
        mov word [tcolor], TXT_WHITE
        mov si, s_paused
        mov ax, 90
        mov ch, 1
        call text_center
.np:    cmp byte [show_fps], 0
        je .nf
        mov word [tcolor], TXT_WHITE
        mov si, s_fps
        mov di, 180*80 + 66
        call draw_text
        mov ax, [fps]
        call utoa
        mov si, numbuf + 3
        mov di, 180*80 + 74
        call draw_text
%ifdef COUNT
        mov ax, [fpuops]
        mov word [fpuops], 0
        call utoa
        mov si, numbuf
        mov di, 170*80 + 70
        call draw_text
%endif
.nf:    pop es
        ret

draw_title:
        mov word [tcolor], TXT_MAGENTA
        mov si, s_title
        mov ax, 22
        mov ch, 1
        call text_center
        mov word [tcolor], TXT_CYAN
        mov si, s_sub
        mov ax, 44
        xor ch, ch
        call text_center
        mov word [tcolor], TXT_WHITE
        test byte [ticks+1], 1
        jz .nb
        mov si, s_press
        mov ax, 104
        xor ch, ch
        call text_center
.nb:    mov si, s_hi
        mov di, 120*80 + 25
        call draw_text
        mov ax, [hiscore]
        mov di, 120*80 + 43
        call draw_score
        mov word [rectpat], 0   ; dark strip behind the help text
        xor ax, ax
        mov bx, 170
        mov cx, SCR_W - 1
        mov dx, 193
        call fill_rect
        mov word [tcolor], TXT_CYAN
        mov si, s_help1
        mov ax, 172
        xor ch, ch
        call text_center
        mov si, s_help2
        mov ax, 184
        xor ch, ch
        call text_center
        ret
