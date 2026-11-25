; =====================================================================
;  STORM RUN  (FLIGHT87.COM)
;  A flat-shaded 3D rail shooter for the IBM PC
;  Target: 8088 @ 4.77 MHz + 8087 FPU, CGA 320x200x4, PC speaker
;
;  All 3D math (rotation matrices, vertex transform, perspective divide,
;  back-face culling, lighting, 3D clipping, ground projection, physics,
;  enemy aiming) runs on the 8087.  The 8088 does integer edge walking,
;  span filling, collisions and game logic - and where it can, it does
;  that *while* the 8087 is busy with a long FDIV.
;
;  Build:  ./build.sh   (python3 tools/gen_data.py; nasm -f bin ...)
; =====================================================================

cpu 8086
org 100h

%include "fpu.inc"

; ---------------------------------------------------------------------
; constants
; ---------------------------------------------------------------------
SCR_W     equ 320
SCR_H     equ 200
HORIZON   equ 78            ; screen row of the horizon (= projection CY)
NEAR_HI   equ 4100h         ; high word of float 8.0 (near plane)
NEAR_I    equ 8
FAR_I     equ 1350
SHIP_Z    equ 92
SPAWN_Z   equ 1400
GROUND_I  equ -45

NENT      equ 48            ; entity pool (slot 0 = player ship)
NPART     equ 28            ; debris particles
MAXPV     equ 16            ; max polygon verts after clipping
MAXVIS    equ 32
SHIELD_MAX equ 24
ROLL_T    equ 64            ; dodge roll duration (ticks)

TICKS_HZ  equ 146           ; our timer runs at 1193182/8192 Hz

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
MF_ARCH   equ 4
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
E_X       equ 4             ; position (floats, camera z = 0)
E_Y       equ 8
E_Z       equ 12
E_YAW     equ 16            ; orientation (8.8 angles)
E_PITCH   equ 18
E_ROLL    equ 20
E_DYAW    equ 22            ; spin per tick
E_DPITCH  equ 24
E_DROLL   equ 26
E_RAMP    equ 28            ; colour ramp, 0FFh = per-face
E_STATE   equ 30            ; >0: exploding (ticks left)
E_KIND    equ 32
E_IX      equ 34            ; integer position copy
E_IY      equ 36
E_IZ      equ 38
E_VX      equ 40            ; velocity (floats, units/s)
E_VY      equ 44
E_VZ      equ 48
E_HP      equ 52            ; 0 = indestructible
E_AI      equ 54
E_T       equ 56            ; age in ticks
E_FIRE    equ 58            ; fire cooldown
E_P0      equ 60            ; AI parameters
E_P1      equ 62
E_P2      equ 64
E_HIT     equ 66            ; hit flash ticks
E_SCORE   equ 68
E_SHOTS   equ 70
E_SIZE    equ 72
; particle record
P_X       equ 0
P_Y       equ 4
P_Z       equ 8
P_VX      equ 12
P_VY      equ 16
P_VZ      equ 20
P_LIFE    equ 24
P_PAT     equ 26
P_SIZE    equ 28

K_SHIP    equ 0
K_OBST    equ 1
K_BOLT    equ 2
K_ENEMY   equ 3
K_EBUL    equ 4
K_PICK    equ 5
K_BOSS    equ 6

AI_NONE   equ 0
AI_RUSH   equ 1
AI_SWOOP  equ 2
AI_HOVER  equ 3
AI_TURRET equ 4
AI_CORE   equ 5
AI_GUN    equ 6
AI_PICK   equ 7

GS_TITLE  equ 0
GS_PLAY   equ 1
GS_DEAD   equ 2
GS_OVER   equ 3
GS_CLEAR  equ 4

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
        push es
        xor ax, ax
        mov es, ax
        mov ax, [es:046Ch]
        pop es
        or ax, 1
        mov [seed], ax
        mov [nseed], ax

        mov ah, 0Fh
        int 10h
        mov [oldmode], al
        mov ax, 0004h           ; CGA 320x200 4 colours
        int 10h

        call install_handlers
        mov ax, [ticks]
        mov [lastticks], ax
        call game_title

main_loop:
        call frame_time
        call global_keys
        cmp byte [paused], 0
        jne .draw
        call game_update
.draw:  call render
        call blit
        cmp byte [quit], 0
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

; Esc, pause, palette, sound, fps
global_keys:
        ; keep the BIOS keyboard buffer empty (no beeps)
        push es
        mov ax, 40h
        mov es, ax
        cli
        mov ax, [es:1Ch]
        mov [es:1Ah], ax
        sti
        pop es

        mov al, 01h             ; Esc: title -> quit, game -> title
        call key_pressed
        jz .k1
        mov byte [paused], 0
        cmp byte [gstate], GS_TITLE
        jne .totitle
        mov byte [quit], 1
        ret
.totitle:
        call game_title
        ret
.k1:    mov al, 19h             ; P: pause
        call key_pressed
        jz .k2
        cmp byte [gstate], GS_TITLE
        je .k2
        xor byte [paused], 1
        call speaker_off
.k2:    mov al, 2Eh             ; C: palette
        call key_pressed
        jz .k3
        xor byte [palsel], 1
        call set_palette
.k3:    mov al, 32h             ; M: sound
        call key_pressed
        jz .k4
        xor byte [mute], 1
        call speaker_off
.k4:    mov al, 21h             ; F: frame rate display
        call key_pressed
        jz .k5
        xor byte [show_fps], 1
.k5:    ; attract mode loops before the boss
        cmp byte [gstate], GS_TITLE
        jne .x
        cmp word [dist], TITLE_LOOP
        jb .x
        call start_stage
        mov word [msg_time], 0
.x:     ret

; ---------------------------------------------------------------------
; tables: row offsets, font expansion (1bpp -> 2bpp, normal and wide)
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

        ; double width: every font pixel becomes 2 pixels (4 bits)
        xor bx, bx
        mov di, expand2
.e2:    mov cl, bl
        xor ax, ax
        xor dx, dx
        mov ch, 8
.b2:    push cx
        mov cx, 4
.sh:    shl ax, 1
        rcl dx, 1
        loop .sh
        pop cx
        shl cl, 1
        jnc .z2
        or ax, 0Fh
.z2:    dec ch
        jnz .b2
        xchg dl, dh
        xchg al, ah
        mov [di], dx
        mov [di+2], ax
        add di, 4
        inc bl
        jnz .e2
        ret

set_palette:
        push ax
        push bx
        mov ah, 0Bh
        mov bh, 1
        mov bl, [palsel]
        int 10h
        mov ah, 0Bh
        mov bh, 0
        mov bl, [bgcolor]
        or bl, 10h              ; intense foreground colours
        int 10h
        pop bx
        pop ax
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
        cmp si, ents            ; player ship: hidden, or blinking
        jne .ins0
        cmp byte [ship_hidden], 0
        jne .cn
        cmp word [invuln], 0
        je .ins0
        test byte [ticks], 8
        jnz .cn
.ins0:  mov ax, [si+E_IZ]
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
        jae .parts
        push bx
        mov si, [order+bx]
        call render_entity
        pop bx
        add bx, 2
        jmp short .d
.parts: call render_particles
        call draw_hud
        ret

%include "system.inc"
%include "engine.inc"
%include "sprites.inc"
%include "game.inc"
%include "hud.inc"
%include "stage.inc"

; =====================================================================
; constants
; =====================================================================
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
c_tail:     dd 0.028            ; laser streak length (seconds of travel)
c_aim:      dd 3.6              ; lasers inherit ship velocity * this
c_aimt:     dd 1.152            ; c_aim * reticle time (0.32 s)
c_retz:     dd 521.0            ; reticle depth
c_bulsz:    dd 4.5              ; bullet radius in world units
c_partsz:   dd 2.5
c_gravity:  dd 260.0
c_rollv:    dd 170.0
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
c_bank:     dd -5000.0
c_07:       dd 0.7
c_06:       dd 0.6
c_14:       dd 30.0
c_70:       dd 70.0
c_60:       dd 60.0
c_45:       dd 45.0
c_25:       dd 25.0
c_24:       dd 24.0
c_18:       dd 18.0
c_15:       dd 15.0
c_10:       dd 10.0
c_m10:      dd -10.0
c_8:        dd 8.0
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

tcolor:     dw 0FFFFh

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
pt3:      resd 3
pk:       resd 1
ftmp:     resd 1
dtf:      resd 1
dzf:      resd 1
gdt:      resd 1
scroll:   resd 1
travel:   resd 1
camx:     resd 1
camy:     resd 1
hfy:      resd 1
svx:      resd 1
svy:      resd 1
dvx:      resd 1
dvy:      resd 1
bulspeed: resd 1
ents:     resb E_SIZE*NENT
parts:    resb P_SIZE*NPART
order:    resw NENT
norder:   resw 1
boltlist: resw 10
nbolt:    resw 1
vislist:  resw 3*MAXVIS
nvis:     resw 1
px:       resw MAXPV
py:       resw MAXPV
pn:       resw 1
xl:       resw SCR_H
xr:       resw SCR_H
rowofs:   resw SCR_H
expand:   resw 256
expand2:  resd 256
keys:     resb 128
keylatch: resb 128
numbuf:   resb 8
old8:     resd 1
old9:     resd 1
ticks:    resw 1
subtick:  resb 1
sfx_ptr:  resw 1
sfx_pri:  resb 1
mute:     resb 1
nseed:    resw 1
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
gstate:   resb 1
paused:   resb 1
quit:     resb 1
show_fps: resb 1
firing:   resb 1
ship_hidden: resb 1
boss_active: resb 1
orcode:   resb 1
tmpw:     resw 1
tmpw2:    resw 1
fsw:      resw 1
fpuops:   resw 1
seed:     resw 1
kx:       resw 1
ky:       resw 1
demo_a:   resw 1
demo_b:   resw 1
cooldown: resw 1
invuln:   resw 1
flash:    resw 1
shield:   resw 1
score:    resw 1
hiscore:  resw 1
lives:    resw 1
bombs:    resw 1
stage:    resw 1
diff:     resw 1
firescale: resw 1
bonus:    resw 1
statetimer: resw 1
msg_ptr:  resw 1
msg_time: resw 1
rollt:    resw 1
rollcd:   resw 1
rolldir:  resw 1
dist:     resw 1
scriptp:  resw 1
field_set: resw 1
field_gap: resw 1
field_spread: resw 1
field_next: resw 1
boss_core: resw 1
boss_guns: resw 1
boss_maxhp: resw 1
w_ai:     resw 1
w_n:      resw 1
w_p:      resw 1
w_i:      resw 1
w_k:      resw 1
w_ak:     resw 1
boff:     resw 1
bul_yofs: resw 1
hxpad:    resw 1
hypad:    resw 1
hzpad:    resw 1
rawdx:    resw 1
rawdy:    resw 1
rectpat:  resw 1
fr_x0:    resw 1
fr_x1:    resw 1
fr_y1:    resw 1
fr_sx:    resw 1
fr_sy:    resw 1
barpat:   resw 1
bar_x:    resw 1
bar_y:    resw 1
bar_w:    resw 1
bar_f:    resw 1
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
