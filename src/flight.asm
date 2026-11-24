; =====================================================================
;  FLIGHT87 - flat-shaded 3D rail shooter demo for the IBM PC
;  Target: 8088 @ 4.77 MHz + 8087 FPU, CGA 320x200x4
;
;  All 3D math (rotation matrices, vertex transform, perspective divide,
;  back-face culling, lighting, 3D clipping, ground projection, ship
;  physics) runs on the 8087.  The 8088 does what it is good at:
;  integer edge walking, span filling, and moving bytes - and where it
;  can, it does that *while* the 8087 is busy with a long FDIV.
;
;  Build:  nasm -f bin -o FLIGHT87.COM src/flight.asm
; =====================================================================

cpu 8086
org 100h

; ---------------------------------------------------------------------
; 8087 instructions.  On an 8088 the CPU must WAIT before every
; coprocessor instruction (the 8087 cannot accept a new opcode while it
; is busy).  MASM did this automatically; here each mnemonic is shadowed
; by a macro that emits WAIT + the real instruction.  Integer code
; placed *between* two FPU instructions therefore runs in parallel with
; the first one.  An explicit FWAIT is still needed before the CPU reads
; a result the 8087 stored to memory.
; ---------------------------------------------------------------------
%macro fldz 0
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fldz
%endmacro
%macro fld 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fld %1
%endmacro
%macro fild 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fild %1
%endmacro
%macro fst 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fst %1
%endmacro
%macro fstp 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fstp %1
%endmacro
%macro fist 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fist %1
%endmacro
%macro fistp 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fistp %1
%endmacro
%macro fadd 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fadd %1
%endmacro
%macro faddp 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  faddp %1
%endmacro
%macro fsub 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fsub %1
%endmacro
%macro fsubp 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fsubp %1
%endmacro
%macro fsubr 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fsubr %1
%endmacro
%macro fsubrp 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fsubrp %1
%endmacro
%macro fmul 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fmul %1
%endmacro
%macro fmulp 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fmulp %1
%endmacro
%macro fdiv 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fdiv %1
%endmacro
%macro fdivp 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fdivp %1
%endmacro
%macro frndint 0
  wait
  frndint
%endmacro
%macro fdivr 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fdivr %1
%endmacro
%macro fchs 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fchs %1
%endmacro
%macro fcom 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fcom %1
%endmacro

; read 8087 condition codes into CPU flags (8087 has no FSTSW AX)
%macro FFLAGS 0
  fstsw [fsw]
  fwait
  mov ah, [fsw+1]
  sahf
%endmacro

; ---------------------------------------------------------------------
; constants
; ---------------------------------------------------------------------
SCR_W     equ 320
SCR_H     equ 200
HORIZON   equ 78            ; screen row of the horizon (= projection CY)
NEAR_HI   equ 4100h         ; high word of float 8.0 (near plane)
NEAR_I    equ 8
FAR_I     equ 1500
SHIP_Z    equ 92

NOBS      equ 12            ; obstacles
NBOLT     equ 6             ; laser bolts
NENT      equ 1 + NOBS + NBOLT
MAXPV     equ 16            ; max polygon verts after clipping
MAXVIS    equ 32

TICKS_HZ  equ 146           ; our timer runs at 1193182/8192 Hz
DEMO_IDLE equ 1200          ; ticks without input -> autopilot

; model header
M_NV      equ 0
M_NF      equ 2
M_FLAGS   equ 4
M_RAD     equ 6
M_HX      equ 8
M_HY      equ 10
M_HZ      equ 12
M_CYO     equ 14
M_FACES   equ 16
M_VERTS   equ 18
MF_GROUND equ 1
MF_SORT   equ 2
; face record
F_NV      equ 0
F_RAMP    equ 1
F_IDX     equ 2
F_NX      equ 8
F_NY      equ 12
F_NZ      equ 16
F_D       equ 20
; entity record
E_ACT     equ 0
E_MODEL   equ 2
E_X       equ 4
E_Y       equ 8
E_Z       equ 12
E_YAW     equ 16
E_PITCH   equ 18
E_ROLL    equ 20
E_DYAW    equ 22
E_DPITCH  equ 24
E_DROLL   equ 26
E_RAMP    equ 28
E_STATE   equ 30
E_KIND    equ 32
E_IX      equ 34
E_IY      equ 36
E_IZ      equ 38
E_SIZE    equ 40

K_SHIP    equ 0
K_OBST    equ 1
K_BOLT    equ 2

; =====================================================================
start:
        cld
        mov di, bss_start
        mov cx, bss_end - bss_start
        xor al, al
        rep stosb

        call detect_fpu
        jnc .fpu_ok
        mov dx, msg_nofpu
        jmp fatal
.fpu_ok:
        ; back buffer lives in the 64K segment after ours
        mov ax, cs
        add ax, 1000h
        mov [backseg], ax
        add ax, 0400h
        cmp ax, [2]             ; PSP: top of memory
        jb .mem_ok
        mov dx, msg_nomem
        jmp fatal
.mem_ok:
        call build_tables

        mov ah, 0Fh
        int 10h
        mov [oldmode], al
        mov ax, 0004h           ; CGA 320x200 4 colours
        int 10h
        mov byte [bgcolor], 1
        mov byte [palsel], 1
        call set_palette

        call install_handlers
        call init_game

main_loop:
        call frame_time
        call update
        call render
        call blit
        cmp byte [keys+01h], 0  ; Esc
        je main_loop

        call remove_handlers
        mov al, [oldmode]
        xor ah, ah
        int 10h
        mov dx, msg_bye
        mov ah, 9
        int 21h
        mov ax, 4C00h
        int 21h

fatal:
        mov ah, 9
        int 21h
        mov ax, 4C01h
        int 21h

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
; tables: row offsets and font expansion (1bpp -> 2bpp colour 3)
; ---------------------------------------------------------------------
build_tables:
        xor ax, ax
        mov di, rowofs
        mov cx, SCR_H
.r:     stosw
        add ax, 80
        loop .r

        xor bx, bx
        mov di, expand
.e:     mov al, bl
        xor dx, dx
        mov cx, 8
.b:     shl dx, 1
        shl dx, 1
        shl al, 1
        jnc .z
        or dx, 3
.z:     loop .b
        xchg dl, dh             ; first 4 pixels -> first byte
        mov [di], dx
        add di, 2
        inc bl
        jnz .e
        ret

set_palette:
        mov ah, 0Bh
        mov bh, 1
        mov bl, [palsel]
        int 10h
        mov ah, 0Bh
        mov bh, 0
        mov bl, [bgcolor]
        or bl, 10h              ; intense foreground colours
        int 10h
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
        inc word [cs:ticks]
        inc byte [cs:subtick]
        test byte [cs:subtick], 7
        jnz .eoi
        pop ax
        jmp far [cs:old8]
.eoi:   mov al, 20h
        out 20h, al
        pop ax
        iret

isr9:
        push ax
        push bx
        in al, 60h
        mov bl, al
        and bx, 007Fh
        test al, 80h
        jnz .rel
        mov byte [cs:keys+bx], 1
        mov byte [cs:keyhit], 1
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
; game setup
; ---------------------------------------------------------------------
init_game:
        push es
        xor ax, ax
        mov es, ax
        mov ax, [es:046Ch]
        pop es
        or ax, 1
        mov [seed], ax
        mov ax, [ticks]
        mov [lastticks], ax

        mov word [shield], 4
        mov word [idle], 60000  ; start in attract/demo mode
        mov byte [demo], 1

        ; ship
        mov si, ents
        mov word [si+E_ACT], 1
        mov word [si+E_MODEL], model_ship
        mov word [si+E_RAMP], 0FFh
        mov word [si+E_KIND], K_SHIP
        fldz
        fst dword [si+E_X]
        fstp dword [si+E_Y]
        mov word [tmpw], SHIP_Z
        fild word [tmpw]
        fstp dword [si+E_Z]
        mov word [si+E_IZ], SHIP_Z

        ; obstacles, staggered in depth
        mov si, ents + E_SIZE
        mov cx, NOBS
        mov dx, 320
.o:     call spawn
        mov [tmpw], dx
        fild word [tmpw]
        fstp dword [si+E_Z]
        mov [si+E_IZ], dx
        add dx, 125
%ifdef NOOBS
        mov word [si+E_ACT], 0
%endif
        add si, E_SIZE
        loop .o

        ; bolts
        mov cx, NBOLT
.b:     mov word [si+E_ACT], 0
        mov word [si+E_MODEL], model_bolt
        mov word [si+E_KIND], K_BOLT
        mov word [si+E_RAMP], 0FFh
        add si, E_SIZE
        loop .b
        fwait
        ret

obst_models:
        dw model_cube, model_pillar, model_pyramid, model_octa
        dw model_diamond, model_hexcol, model_tetra, model_archtop

; spawn a new obstacle far away.  SI = entity
spawn:
        push ax
        push bx
        push cx
        push dx
        mov word [si+E_ACT], 1
        mov word [si+E_KIND], K_OBST
        mov word [si+E_STATE], 0
        mov cx, 8
        call rand_mod
        mov bx, ax
        shl bx, 1
        mov bx, [obst_models+bx]
        mov [si+E_MODEL], bx

        ; x: half of them in the flight lane
        call rand
        test al, 1
        jz .wide
        mov cx, 150
        call rand_mod
        sub ax, 75
        jmp short .setx
.wide:  mov cx, 500
        call rand_mod
        sub ax, 250
.setx:  mov [si+E_IX], ax
        mov [tmpw], ax
        fild word [tmpw]
        fstp dword [si+E_X]

        mov cx, 350
        call rand_mod
        add ax, 1550
        mov [si+E_IZ], ax
        mov [tmpw], ax
        fild word [tmpw]
        fstp dword [si+E_Z]

        mov cx, 256
        call rand_mod
        mov ah, al
        xor al, al
        mov [si+E_YAW], ax

        test word [bx+M_FLAGS], MF_GROUND
        jz .air
        mov ax, -45             ; sits on the ground
        mov word [si+E_PITCH], 0
        mov word [si+E_ROLL], 0
        mov word [si+E_DPITCH], 0
        mov word [si+E_DROLL], 0
        mov cx, 80
        call rand_mod
        sub ax, 40
        mov [si+E_DYAW], ax
        mov ax, -45
        jmp short .sety
.air:   call rand
        mov [si+E_PITCH], ax
        call rand
        mov [si+E_ROLL], ax
        mov cx, 300
        call rand_mod
        sub ax, 150
        mov [si+E_DYAW], ax
        mov cx, 300
        call rand_mod
        sub ax, 150
        mov [si+E_DPITCH], ax
        mov cx, 200
        call rand_mod
        sub ax, 100
        mov [si+E_DROLL], ax
        mov cx, 85
        call rand_mod
        sub ax, 25
.sety:  mov [si+E_IY], ax
        mov [tmpw], ax
        fild word [tmpw]
        fstp dword [si+E_Y]

        mov cx, 3
        call rand_mod
        mov [si+E_RAMP], ax
        fwait
        pop dx
        pop cx
        pop bx
        pop ax
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

; ---------------------------------------------------------------------
; simulation
; ---------------------------------------------------------------------
update:
        ; keep the BIOS keyboard buffer empty (no beeps)
        push es
        mov ax, 40h
        mov es, ax
        cli
        mov ax, [es:1Ch]
        mov [es:1Ah], ax
        sti
        pop es

        ; palette toggle (P), edge triggered
        mov al, [keys+19h]
        cmp al, [pkey_prev]
        je .nop
        mov [pkey_prev], al
        test al, al
        jz .nop
        xor byte [palsel], 1
        call set_palette
.nop:
        ; directional input
        xor ax, ax
        mov al, [keys+4Dh]
        or al, [keys+20h]
        mov bl, [keys+4Bh]
        or bl, [keys+1Eh]
        sub al, bl
        cbw
        mov [kx], ax
        mov al, [keys+48h]
        or al, [keys+11h]
        mov bl, [keys+50h]
        or bl, [keys+1Fh]
        sub al, bl
        cbw
        mov [ky], ax
        mov al, [keys+48h]
        or al, [keys+50h]
        or al, [keys+4Bh]
        or al, [keys+4Dh]
        or al, [keys+39h]
        or al, [keys+11h]
        or al, [keys+1Fh]
        or al, [keys+1Eh]
        or al, [keys+20h]
        or al, [keyhit]
        mov byte [keyhit], 0
        jz .noinput
        mov word [idle], 0
        mov byte [demo], 0
        jmp short .inp_done
.noinput:
        mov ax, [idle]
        add ax, [dtt]
        jc .sat
        mov [idle], ax
.sat:   cmp word [idle], DEMO_IDLE
        jb .inp_done
        mov byte [demo], 1
.inp_done:
        call ship_physics
        call move_world
        call fire_control
        call int_coords
        call collisions
        ; flash timer (background colour)
        cmp word [flash], 0
        je .nf
        mov ax, [dtt]
        sub [flash], ax
        ja .nf
        mov word [flash], 0
        mov byte [bgcolor], 1
        call set_palette
.nf:    ret

ship_physics:
        cmp byte [demo], 0
        je .manual
        ; autopilot: weave along a Lissajous path
        mov ax, [dtt]
        mov cx, 95
        mul cx
        add [demo_a], ax
        mov ax, [dtt]
        mov cx, 151
        mul cx
        add [demo_b], ax
        mov ax, [demo_a]
        call angptr
        fld dword [bx]
        fmul dword [c_60]
        mov ax, [demo_a]
        mov dx, ax
        shl ax, 1
        add ax, dx
        call angptr
        fld dword [bx]
        fmul dword [c_15]
        faddp st1, st0
        fsub dword [ents+E_X]
        fmul dword [c_3]
        mov si, lim_vx
        call fclamp
        fstp dword [dvx]
        mov ax, [demo_b]
        call angptr
        fld dword [bx]
        fmul dword [c_24]
        fadd dword [c_4]
        fsub dword [ents+E_Y]
        fmul dword [c_3]
        mov si, lim_vy
        call fclamp
        fstp dword [dvy]
        jmp short .phys
.manual:
        fild word [kx]
        fmul dword [c_vx]
        fstp dword [dvx]
        fild word [ky]
        fmul dword [c_vy]
        fstp dword [dvy]
.phys:
        fld dword [dtf]
        fmul dword [c_6]
        mov si, lim_01
        call fclamp             ; st0 = smoothing factor
        fld dword [dvx]
        fsub dword [svx]
        fmul st0, st1
        fadd dword [svx]
        fst dword [svx]
        fmul dword [dtf]
        fadd dword [ents+E_X]
        mov si, lim_x
        call fclamp
        fstp dword [ents+E_X]
        test al, al             ; hit the edge of the flight box: stop
        jz .nx
        fldz
        fstp dword [svx]
.nx:
        fld dword [dvy]
        fsub dword [svy]
        fmul st0, st1
        fadd dword [svy]
        fst dword [svy]
        fmul dword [dtf]
        fadd dword [ents+E_Y]
        mov si, lim_y
        call fclamp
        fstp dword [ents+E_Y]
        test al, al             ; hit the edge of the flight box: stop
        jz .ny
        fldz
        fstp dword [svy]
.ny:
        fstp st0
        ; bank, pitch and yaw follow the velocity
        fld dword [svx]
        fmul dword [c_kroll]
        fistp word [ents+E_ROLL]
        fld dword [svy]
        fmul dword [c_kpitch]
        fistp word [ents+E_PITCH]
        fld dword [svx]
        fmul dword [c_kyaw]
        fistp word [ents+E_YAW]
        ; chase camera
        fld dword [ents+E_X]
        fmul dword [c_07]
        fst dword [camx]
        fistp word [icamx]
        fld dword [ents+E_Y]
        fmul dword [c_06]
        fadd dword [c_14]
        fst dword [camy]
        fistp word [icamy]
        fwait
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

move_world:
        fld dword [c_speed]
        fmul dword [dtf]
        fst dword [dzf]
        fadd dword [scroll]
        fcom dword [c_2sp]
        FFLAGS
        jb .s
        fsub dword [c_2sp]
.s:     fstp dword [scroll]
        fld dword [c_boltspeed]
        fmul dword [dtf]
        fstp dword [dzb]

        mov si, ents + E_SIZE
        mov cx, NOBS + NBOLT
.l:     cmp word [si+E_ACT], 0
        je .n
        ; spin
        mov bx, [dtt]
        mov ax, [si+E_DYAW]
        imul bx
        add [si+E_YAW], ax
        mov ax, [si+E_DPITCH]
        imul bx
        add [si+E_PITCH], ax
        mov ax, [si+E_DROLL]
        imul bx
        add [si+E_ROLL], ax
        fld dword [si+E_Z]
        cmp word [si+E_KIND], K_BOLT
        je .bolt
        fsub dword [dzf]
        jmp short .st
.bolt:  fadd dword [dzb]
.st:    fstp dword [si+E_Z]
.n:     add si, E_SIZE
        loop .l
        fwait
        ret

fire_control:
        mov ax, [cooldown]
        sub ax, [dtt]
        jnc .c
        xor ax, ax
.c:     mov [cooldown], ax
        test ax, ax
        jnz .x
        mov al, [keys+39h]
        or al, [keys+1Dh]
        or al, [demo]
        jz .x
        mov word [cooldown], 22
        cmp byte [demo], 0
        je .f
        mov word [cooldown], 50
.f:     fld dword [c_10]
        call fire_one
        fld dword [c_m10]
        call fire_one
.x:     ret

; st0 = x offset (popped).  Launch a bolt from the wing.
fire_one:
        mov si, ents + E_SIZE * (1 + NOBS)
        mov cx, NBOLT
.l:     cmp word [si+E_ACT], 0
        je .found
        add si, E_SIZE
        loop .l
        fstp st0
        ret
.found: mov word [si+E_ACT], 1
        mov word [si+E_YAW], 0
        mov word [si+E_PITCH], 0
        mov word [si+E_ROLL], 0
        mov word [si+E_DYAW], 0
        mov word [si+E_DPITCH], 0
        mov word [si+E_DROLL], 0
        fadd dword [ents+E_X]
        fstp dword [si+E_X]
        fld dword [ents+E_Y]
        fsub dword [c_2]
        fstp dword [si+E_Y]
        fld dword [c_boltz]
        fstp dword [si+E_Z]
        fwait
        mov word [si+E_IZ], SHIP_Z + 30
        ret

; integer copies of all positions; recycle things that left the scene
int_coords:
        mov si, ents
        mov cx, NENT
.l:     cmp word [si+E_ACT], 0
        je .n
        fld dword [si+E_X]
        fistp word [si+E_IX]
        fld dword [si+E_Y]
        fistp word [si+E_IY]
        fld dword [si+E_Z]
        fistp word [si+E_IZ]
        fwait
        mov ax, [si+E_KIND]
        cmp ax, K_OBST
        jne .b
        cmp word [si+E_IZ], -80
        jl .respawn
        mov ax, [si+E_STATE]
        test ax, ax
        jz .n
        sub ax, [dtt]
        mov [si+E_STATE], ax
        jg .n
.respawn:
        call spawn
        jmp short .n
.b:     cmp ax, K_BOLT
        jne .n
        cmp word [si+E_IZ], 2400
        jl .n
        mov word [si+E_ACT], 0
.n:     add si, E_SIZE
        loop .l
        ret

; |AX| -> AX
%macro ABS_AX 0
        cwd
        xor ax, dx
        sub ax, dx
%endmacro

collisions:
        ; obstacle loop
        mov si, ents + E_SIZE
        mov cx, NOBS
.ol:    cmp word [si+E_ACT], 0
        je .on
        cmp word [si+E_STATE], 0
        jne .on
        mov bx, [si+E_MODEL]
        ; --- bolts vs this obstacle
        mov di, ents + E_SIZE * (1 + NOBS)
        mov dx, NBOLT
.bl:    cmp word [di+E_ACT], 0
        je .bn
        mov ax, [di+E_IZ]
        sub ax, [si+E_IZ]
        push dx
        ABS_AX
        pop dx
        sub ax, 16
        cmp ax, [bx+M_HZ]
        jg .bn
        mov ax, [di+E_IX]
        sub ax, [si+E_IX]
        push dx
        ABS_AX
        pop dx
        sub ax, 4
        cmp ax, [bx+M_HX]
        jg .bn
        mov ax, [di+E_IY]
        sub ax, [si+E_IY]
        sub ax, [bx+M_CYO]
        push dx
        ABS_AX
        pop dx
        sub ax, 4
        cmp ax, [bx+M_HY]
        jg .bn
        ; hit!
        mov word [di+E_ACT], 0
        call explode
        add word [score], 100
        jmp short .on
.bn:    add di, E_SIZE
        dec dx
        jnz .bl
        ; --- ship vs this obstacle
        cmp word [invuln], 0
        jne .on
        mov ax, [si+E_IZ]
        sub ax, SHIP_Z
        ABS_AX
        sub ax, 18
        cmp ax, [bx+M_HZ]
        jg .on
        mov ax, [si+E_IX]
        sub ax, [ents+E_IX]
        ABS_AX
        sub ax, 18
        cmp ax, [bx+M_HX]
        jg .on
        mov ax, [si+E_IY]
        add ax, [bx+M_CYO]
        sub ax, [ents+E_IY]
        ABS_AX
        sub ax, 6
        cmp ax, [bx+M_HY]
        jg .on
        call explode
        mov word [invuln], 90
        mov word [flash], 30
        mov byte [bgcolor], 4   ; red sky
        push cx
        call set_palette
        pop cx
        dec word [shield]
        jnz .on
        mov word [shield], 4
        mov word [score], 0
.on:    add si, E_SIZE
        dec cx
        jz .done
        jmp .ol
.done:
        mov ax, [invuln]
        sub ax, [dtt]
        jnc .iv
        xor ax, ax
.iv:    mov [invuln], ax
        ret

explode:
        mov word [si+E_STATE], 45
        mov word [si+E_RAMP], RAMP_FLASH
        mov word [si+E_DYAW], 700
        mov word [si+E_DPITCH], 500
        mov word [si+E_DROLL], 300
        ret

; =====================================================================
; rendering
; =====================================================================
render:
        call render_bg
        ; depth sort active entities (insertion sort, far to near)
        xor bx, bx              ; count*2
        mov si, ents
        mov cx, NENT
.c:     cmp word [si+E_ACT], 0
        je .cn
        mov ax, [si+E_IZ]
        mov di, bx
.ins:   test di, di
        jz .put
        mov dx, [order+di-2]
        push si
        mov si, dx
        cmp ax, [si+E_IZ]
        pop si
        jle .put
        mov [order+di], dx
        sub di, 2
        jmp short .ins
.put:   mov [order+di], si
        add bx, 2
.cn:    add si, E_SIZE
        loop .c
        mov [norder], bx

        xor bx, bx
.d:     cmp bx, [norder]
        jae .hud
        push bx
        mov si, [order+bx]
        call render_entity
        pop bx
        add bx, 2
        jmp short .d
.hud:   call draw_hud
        ret

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
        cmp word [si+E_KIND], K_BOLT
        jne .solid
        jmp render_bolt
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
        mov ax, [si+E_RAMP]
        mov [cur_ramp], ax
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

; ---------------------------------------------------------------------
; laser bolt: project head and tail points, draw a tapered streak
; ---------------------------------------------------------------------
render_bolt:
        mov ax, [si+E_IZ]
        cmp ax, 40
        jge .b1
        ret
.b1:    cmp ax, FAR_I
        jle .b2
        ret
.b2:    fld dword [si+E_X]
        fsub dword [camx]
        fstp dword [tx]
        fld dword [si+E_Y]
        fsub dword [camy]
        fmul dword [c_asp]
        fstp dword [ty]
        ; head (far end) and tail (near end)
        fld dword [si+E_Z]
        fadd dword [c_boltlen]
        call .proj
        mov [px], ax
        mov [py], dx
        fld dword [si+E_Z]
        fsub dword [c_boltlen]
        call .proj
        mov [px+4], ax
        mov [py+4], dx
        ; reject if off screen (bolts are tiny, no clipping needed)
        mov bx, 0
.ck:    mov ax, [px+bx]
        cmp ax, SCR_W - 4
        jbe .ok1
        ret
.ok1:   cmp ax, 3
        jae .ok2
        ret
.ok2:   mov ax, [py+bx]
        cmp ax, SCR_H - 1
        jbe .ok3
        ret
.ok3:   add bx, 4
        cmp bx, 8
        jb .ck
        ; quad: head is 1 pixel wide, tail 3 pixels
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
; st0 = z (popped)  ->  ax = sx, dx = sy
.proj:  fdivr dword [c_fx]
        fld st0
        fmul dword [tx]
        fadd dword [c_cx]
        fistp word [tmpw]
        fmul dword [ty]
        fsubr dword [c_cy]
        fistp word [tmpw2]
        fwait
        mov ax, [tmpw]
        mov dx, [tmpw2]
        ret

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
; HUD (ROM 8x8 font at F000:FA6E)
; ---------------------------------------------------------------------
; si = asciiz string, di = backbuffer offset
draw_text:
.l:     lodsb
        test al, al
        jz .x
        call draw_char
        add di, 2
        jmp short .l
.x:     ret

draw_char:
        push si
        push di
        push cx
        push ds
        xor ah, ah
        mov si, ax
        shl si, 1
        shl si, 1
        shl si, 1
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
        and [es:di+80], bx      ; drop shadow
        not bx
        or [es:di], bx
        add di, 80
        loop .r
        pop ds
        pop cx
        pop di
        pop si
        ret

; ax -> 5 decimal digits at numbuf
utoa:
        mov di, numbuf + 4
        mov cx, 5
        mov bx, 10
.l:     xor dx, dx
        div bx
        add dl, '0'
        mov [di], dl
        dec di
        loop .l
        ret

draw_hud:
        push es
        mov es, [backseg]
        mov si, s_score
        mov di, 3*80 + 1
        call draw_text
        mov ax, [score]
        call utoa
        mov si, numbuf
        mov di, 3*80 + 14
        call draw_text

        mov si, s_fps
        mov di, 3*80 + 62
        call draw_text
        mov ax, [fps]
        call utoa
        mov si, numbuf + 2
        mov di, 3*80 + 70
        call draw_text

        mov si, s_shield
        mov di, 189*80 + 1
        call draw_text
        mov cx, [shield]
        mov di, 189*80 + 16
        jcxz .ns
.sh:    mov al, '#'              ; ROM font only has chars 0..127
        call draw_char
        add di, 2
        loop .sh
.ns:
        mov si, s_tag
        mov di, 189*80 + 62
        call draw_text
%ifdef COUNT
        mov ax, [fpuops]
        mov word [fpuops], 0
        call utoa
        mov si, numbuf
        mov di, 170*80 + 62
        call draw_text
%endif
        cmp byte [demo], 0
        je .x
        test byte [ticks+1], 1  ; blink
        jz .x
        mov si, s_demo
        mov di, 14*80 + 15
        call draw_text
.x:     pop es
        ret

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

; =====================================================================
; data
; =====================================================================
msg_nofpu: db "FLIGHT87 needs an 8087 (or later) math coprocessor.", 13, 10, "$"
msg_nomem: db "Not enough memory.", 13, 10, "$"
msg_bye:   db "FLIGHT87 - 8088 + 8087 flat-shaded 3D demo.", 13, 10, "$"
s_score:   db "SCORE", 0
s_fps:     db "FPS", 0
s_shield:  db "SHIELD", 0
s_tag:     db "8087", 0
s_demo:    db "DEMO - ARROWS/SPACE TO FLY", 0

align 4
c_tick:     dd 0.0068659        ; seconds per timer tick (8192/1193182)
c_fx:       dd 200.0            ; focal length x
c_cx:       dd 160.0
c_cy:       dd 78.0
c_asp:      dd 0.84             ; FY/FX (CGA pixels are tall)
c_fy:       dd 168.0
c_ground:   dd -45.0
c_speed:    dd 330.0
c_boltspeed: dd 1300.0
c_boltz:    dd 105.0
c_boltlen:  dd 18.0
c_2sp:      dd 120.0            ; two ground stripes
c_sp:       dd 60.0             ; stripe depth
c_invsp:    dd 0.0166667        ; 1/60
c_fogz:     dd 700.0
c_lx:       dd -0.4206
c_ly:       dd 0.7210
c_lz:       dd -0.5508
c_shk:      dd 3.4
c_shb:      dd 3.1
c_vx:       dd 110.0
c_vy:       dd 80.0
c_kroll:    dd -70.0
c_kpitch:   dd -55.0
c_kyaw:     dd 18.0
c_07:       dd 0.7
c_06:       dd 0.6
c_14:       dd 30.0
c_60:       dd 60.0
c_15:       dd 15.0
c_24:       dd 24.0
c_10:       dd 10.0
c_m10:      dd -10.0
c_6:        dd 6.0
c_4:        dd 4.0
c_3:        dd 3.0
c_2:        dd 2.0
lim_01:     dd 0.0, 1.0
lim_vx:     dd -110.0, 110.0
lim_vy:     dd -80.0, 80.0
lim_x:      dd -75.0, 75.0
lim_y:      dd -28.0, 40.0
c_inv:      dd 0.0, 1.0, 0.5, 0.3333333, 0.25, 0.2, 0.1666667

; clip planes a*x + b*y + c*z + d >= 0   (near, left, right, top, bottom)
clip_planes:
        dd 0.0, 0.0, 1.0, -8.0
        dd 200.0, 0.0, 160.0, 0.0
        dd -200.0, 0.0, 159.0, 0.0
        dd 0.0, -168.0, 78.0, 0.0
        dd 0.0, 168.0, 121.0, 0.0

%include "data.inc"

; =====================================================================
section .bss
alignb 4
bss_start:
vfx:      resd MAXV
vfy:      resd MAXV
vfz:      resd MAXV
sxd:      resd MAXV
syd:      resd MAXV
vcode:    resd MAXV
vneed:    resd MAXV
clipa:    resb 12*MAXPV
clipb:    resb 12*MAXPV
cdist:    resd MAXPV
mat:      resd 9
eye:      resd 3
lgt:      resd 3
tx:       resd 1
ty:       resd 1
tz:       resd 1
ftmp:     resd 1
dtf:      resd 1
dzf:      resd 1
dzb:      resd 1
scroll:   resd 1
camx:     resd 1
camy:     resd 1
hfy:      resd 1
svx:      resd 1
svy:      resd 1
dvx:      resd 1
dvy:      resd 1
ents:     resb E_SIZE*NENT
order:    resw NENT
norder:   resw 1
vislist:  resw 3*MAXVIS
nvis:     resw 1
px:       resw MAXPV
py:       resw MAXPV
pn:       resw 1
xl:       resw SCR_H
xr:       resw SCR_H
rowofs:   resw SCR_H
expand:   resw 256
keys:     resb 128
numbuf:   resb 6
old8:     resd 1
old9:     resd 1
ticks:    resw 1
subtick:  resb 1
keyhit:   resb 1
lastticks: resw 1
dtt:      resw 1
frames:   resw 1
fpsacc:   resw 1
fps:      resw 1
backseg:  resw 1
oldmode:  resb 1
palsel:   resb 1
bgcolor:  resb 1
demo:     resb 1
pkey_prev: resb 1
orcode:   resb 1
tmpw:     resw 1
tmpw2:    resw 1
fsw:      resw 1
fpuops:   resw 1
seed:     resw 1
kx:       resw 1
ky:       resw 1
idle:     resw 1
demo_a:   resw 1
demo_b:   resw 1
cooldown: resw 1
invuln:   resw 1
flash:    resw 1
shield:   resw 1
score:    resw 1
icamx:    resw 1
icamy:    resw 1
cur_model: resw 1
cur_ramp: resw 1
curpat:   resw 1
lev:      resw 1
gm:       resw 1
gy:       resw 1
cury:     resw 1
ymin:     resw 1
ymax:     resw 1
ek:       resw 1
eq:       resw 1
e_y0:     resw 1
csrc:     resw 1
cdst:     resw 1
bss_end:
