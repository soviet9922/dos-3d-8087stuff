; =====================================================================
; system: FPU detect, timer/keyboard interrupts, sound, random, timing
; =====================================================================

; ---------------------------------------------------------------------
; FPU detection (classic Intel method).  CF=1 -> no 8087
; ---------------------------------------------------------------------
detect_fpu:
        mov word [tmpw], 5A5Ah
        fninit
        mov cx, 40
.d1:    loop .d1
        fnstsw [tmpw]
        mov cx, 40
.d2:    loop .d2
        cmp byte [tmpw], 0
        jne .none
        fnstcw [tmpw]
        mov cx, 40
.d3:    loop .d3
        mov ax, [tmpw]
        and ax, 103Fh
        cmp ax, 003Fh
        jne .none
        clc
        ret
.none:  stc
        ret

; ---------------------------------------------------------------------
; interrupt handlers
;   INT 8: PIT reprogrammed to 145.6 Hz, chains BIOS every 8th tick
;   INT 9: tracks key up/down state, then chains to the BIOS
; ---------------------------------------------------------------------
install_handlers:
        push es
        mov ax, 3508h
        int 21h
        mov [old8], bx
        mov [old8+2], es
        mov ax, 3509h
        int 21h
        mov [old9], bx
        mov [old9+2], es
        pop es
        mov ax, 2508h
        mov dx, isr8
        int 21h
        mov ax, 2509h
        mov dx, isr9
        int 21h
        cli
        mov al, 36h
        out 43h, al
        mov al, 00h             ; divisor 8192 = 2000h
        out 40h, al
        mov al, 20h
        out 40h, al
        sti
        ret

remove_handlers:
        call speaker_off
        cli
        mov al, 36h
        out 43h, al
        xor al, al
        out 40h, al
        out 40h, al
        sti
        push ds
        lds dx, [old8]
        mov ax, 2508h
        int 21h
        pop ds
        push ds
        lds dx, [old9]
        mov ax, 2509h
        int 21h
        pop ds
        ; flush BIOS keyboard buffer
.fl:    mov ah, 1
        int 16h
        jz .done
        xor ah, ah
        int 16h
        jmp .fl
.done:  ret

isr8:
        push ax
        push bx
        push dx
        inc word [cs:ticks]
        ; --- PC speaker sound sequencer: one table entry per tick
        ;     0 = end, 1 = noise, else PIT channel 2 divisor
        mov bx, [cs:sfx_ptr]
        test bx, bx
        jz .snd_done
        mov ax, [cs:bx]
        test ax, ax
        jz .snd_off
        cmp ax, 1
        jne .tone
        mov ax, [cs:nseed]
        mov dx, 25173
        mul dx
        add ax, 13849
        mov [cs:nseed], ax
        mov al, ah
        and al, 0Fh
        add al, 2
        mov ah, al
        xor al, al              ; random divisor 512..4352
.tone:  mov dx, ax
        mov al, 0B6h
        out 43h, al
        mov al, dl
        out 42h, al
        mov al, dh
        out 42h, al
        in al, 61h
        or al, 3
        out 61h, al
        add word [cs:sfx_ptr], 2
        jmp short .snd_done
.snd_off:
        in al, 61h
        and al, 0FCh
        out 61h, al
        mov word [cs:sfx_ptr], 0
.snd_done:
        inc byte [cs:subtick]
        test byte [cs:subtick], 7
        jnz .eoi
        pop dx
        pop bx
        pop ax
        jmp far [cs:old8]
.eoi:   mov al, 20h
        out 20h, al
        pop dx
        pop bx
        pop ax
        iret

; start a sound effect: BX = table, AL = priority
sfx_play:
        cmp byte [mute], 0
        jne .x
        cli
        cmp word [sfx_ptr], 0
        je .go
        cmp al, [sfx_pri]
        jb .busy
.go:    mov [sfx_ptr], bx
        mov [sfx_pri], al
.busy:  sti
.x:     ret

%macro SFX 2
        push ax
        push bx
        mov bx, %1
        mov al, %2
        call sfx_play
        pop bx
        pop ax
%endmacro

speaker_off:
        cli
        mov word [sfx_ptr], 0
        in al, 61h
        and al, 0FCh
        out 61h, al
        sti
        ret

isr9:
        push ax
        push bx
        in al, 60h
        mov bl, al
        and bx, 007Fh
        test al, 80h
        jnz .rel
        mov byte [cs:keys+bx], 1
        mov byte [cs:keylatch+bx], 1    ; remembered until consumed
        jmp short .done
.rel:   mov byte [cs:keys+bx], 0
.done:  pop bx
        pop ax
        jmp far [cs:old9]

; ---------------------------------------------------------------------
; random numbers (16-bit LCG) -> AX
; ---------------------------------------------------------------------
rand:
        push dx
        mov ax, [seed]
        mov dx, 25173
        mul dx
        add ax, 13849
        mov [seed], ax
        mov al, ah              ; mix: high byte is the good one
        xor ah, [seed]
        pop dx
        ret

; AX = rand() mod CX
rand_mod:
        push dx
        call rand
        xor dx, dx
        div cx
        mov ax, dx
        pop dx
        ret

; ---------------------------------------------------------------------
; frame timing: dtt = ticks since last frame (1..15), dtf = seconds
; ---------------------------------------------------------------------
frame_time:
.w:     mov ax, [ticks]
        sub ax, [lastticks]
        jz .w
        add [lastticks], ax
        cmp ax, 15
        jbe .ok
        mov ax, 15
.ok:    mov [dtt], ax
        fild word [dtt]
        fmul dword [c_tick]
        fstp dword [dtf]
        inc word [frames]
        add [fpsacc], ax
        cmp word [fpsacc], TICKS_HZ
        jb .x
        sub word [fpsacc], TICKS_HZ
        mov ax, [frames]
        mov [fps], ax
        mov word [frames], 0
.x:     fwait
        ret

; clamp st0 to [si] .. [si+4];  AL = 1 if it had to clamp
fclamp:
        fcom dword [si]
        FFLAGS
        jae .lo_ok
        fstp st0
        fld dword [si]
        mov al, 1
        ret
.lo_ok: fcom dword [si+4]
        FFLAGS
        jbe .ok
        fstp st0
        fld dword [si+4]
        mov al, 1
        ret
.ok:    mov al, 0
        ret

; AX = angle (8.8) -> BX = &sintab[AH]; cos is at [BX+256]
angptr:
        mov bl, ah
        xor bh, bh
        shl bx, 1
        shl bx, 1
        add bx, sintab
        ret

