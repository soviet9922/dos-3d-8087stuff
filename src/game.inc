; =====================================================================
; game logic: states, player, enemies, stage script, collisions
; =====================================================================

; ---------------------------------------------------------------------
; game states
; ---------------------------------------------------------------------
clear_latches:
        push di
        push cx
        mov di, keylatch
        mov cx, 64
        xor ax, ax
        rep stosw
        pop cx
        pop di
        ret

game_title:
        call clear_latches
        mov byte [gstate], GS_TITLE
        mov word [stage], 1
        call start_stage
        mov word [msg_time], 0
        ret

new_game:
        call clear_latches
        mov word [score], 0
        mov word [lives], 3
        mov word [bombs], 3
        mov word [stage], 1
        mov word [shield], SHIELD_MAX
        mov byte [gstate], GS_PLAY
        call start_stage
        mov word [invuln], 150
%ifdef FRAGILE                  ; debug: one ship, almost no shield
        mov word [lives], 1
        mov word [shield], 2
        mov word [invuln], 0
%endif
        SFX sfx_start, 5
        ret

; reset the world for the current stage
start_stage:
        mov di, ents + E_SIZE
        mov cx, (NENT - 1) * E_SIZE / 2
        xor ax, ax
        rep stosw
        mov di, parts
        mov cx, NPART * P_SIZE / 2
        rep stosw
        mov [dist], ax
        mov [field_set], ax
        mov [boss_guns], ax
        mov [boss_core], ax
        mov byte [boss_active], 0
        mov [rollt], ax
        mov [rollcd], ax
        mov [cooldown], ax
        fldz
        fstp dword [travel]
        mov word [scriptp], stage_script
%ifdef STARTDIST                ; debug: begin part-way into the stage
        cmp byte [gstate], GS_PLAY
        jne .sd
        mov word [dist], STARTDIST
        SETF [travel], STARTDIST
        mov si, stage_script
.skip:  cmp word [si], STARTDIST
        jae .sk
        add si, 10
        jmp .skip
.sk:    mov [scriptp], si
.sd:
%endif
        call reset_ship
        ; difficulty: bullets get faster, enemies fire more often
        mov ax, [stage]
        dec ax
        cmp ax, 6
        jbe .d
        mov ax, 6
.d:     mov [diff], ax
        mov cx, 45
        mul cx
        add ax, 360
        SETF [bulspeed], ax
        mov ax, [diff]
        mov cx, 11
        mul cx
        mov cx, 100
        sub cx, ax
        mov [firescale], cx
        ; alternate palettes per stage
        mov al, [stage]
        and al, 1
        mov [palsel], al
        mov byte [bgcolor], 1
        call set_palette
        ; "STAGE n"
        mov ax, [stage]
        call utoa
        mov ax, [numbuf+3]
        mov [s_stage_n], ax
        mov word [msg_ptr], s_stage
        mov word [msg_time], 330
        fwait
        ret

reset_ship:
        mov si, ents
        mov word [si+E_ACT], 1
        mov word [si+E_MODEL], model_ship
        mov word [si+E_RAMP], 0FFh
        mov word [si+E_KIND], K_SHIP
        mov word [si+E_HIT], 0
        mov word [si+E_STATE], 0
        fldz
        fst dword [si+E_X]
        fst dword [si+E_Y]
        fst dword [svx]
        fstp dword [svy]
        SETF [si+E_Z], SHIP_Z
        mov word [si+E_IZ], SHIP_Z
        mov byte [ship_hidden], 0
        fwait
        ret

; AX = base cooldown in ticks -> scaled by difficulty
cd_scaled:
        push dx
        mul word [firescale]
        mov cx, 100
        div cx
        pop dx
        ret

; ---------------------------------------------------------------------
; per-frame update
; ---------------------------------------------------------------------
game_update:
        call player_control
        call ship_physics
        call world_advance
        call run_script
        call field_tick
        call update_entities
        call update_particles
        call fire_control
        call collisions
        call state_tick
        ret

; edge-triggered key test: AL = scancode -> ZF=0 if newly pressed
key_pressed:
        xor ah, ah
        mov bx, ax
        xor al, al
        xchg al, [keylatch+bx]  ; read and clear the latch atomically
        test al, al
        ret

player_control:
        mov byte [firing], 0
        mov word [kx], 0
        mov word [ky], 0
        mov al, [gstate]
        cmp al, GS_TITLE
        je .auto
        cmp al, GS_PLAY
        je .manual
        ret
.auto:  ; attract mode: the autopilot flies and shoots
        mov byte [demo], 1
        test byte [ticks], 32
        jz .x
        mov byte [firing], 1
.x:     ret
.manual:
        mov byte [demo], 0
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
        mov al, [keys+39h]
        or al, [keys+1Dh]
        mov [firing], al
        ; dodge roll
        cmp word [rollcd], 0
        jne .nr
        mov al, 2Ch             ; Z: roll left
        call key_pressed
        mov dx, -1
        jnz .roll
        mov al, 2Dh             ; X: roll right
        call key_pressed
        mov dx, 1
        jz .nr
.roll:  mov [rolldir], dx
        mov word [rollt], ROLL_T
        mov word [rollcd], ROLL_T + 45
        SFX sfx_roll, 2
.nr:    ; bomb
        mov al, 30h             ; B
        call key_pressed
        jz .nbm
        cmp word [bombs], 0
        je .nbm
        call do_bomb
.nbm:   ret

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
        jmp .phys
.manual:
        fild word [kx]
        fmul dword [c_vx]
        fstp dword [dvx]
        fild word [ky]
        fmul dword [c_vy]
        fstp dword [dvy]
        cmp word [rollt], 0     ; rolling: quick sidestep
        je .phys
        fild word [rolldir]
        fmul dword [c_rollv]
        fadd dword [dvx]
        fstp dword [dvx]
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
.nx:    fld dword [dvy]
        fsub dword [svy]
        fmul st0, st1
        fadd dword [svy]
        fst dword [svy]
        fmul dword [dtf]
        fadd dword [ents+E_Y]
        mov si, lim_y
        call fclamp
        fstp dword [ents+E_Y]
        test al, al
        jz .ny
        fldz
        fstp dword [svy]
.ny:    fstp st0
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
        fld dword [ents+E_X]
        fist word [ents+E_IX]
        fmul dword [c_07]       ; chase camera
        fst dword [camx]
        fistp word [icamx]
        fld dword [ents+E_Y]
        fist word [ents+E_IY]
        fmul dword [c_06]
        fadd dword [c_14]
        fst dword [camy]
        fistp word [icamy]
        fwait
        ; dodge roll: spin a full turn on top of the bank
        mov ax, [rollt]
        test ax, ax
        jz .x
        sub ax, [dtt]
        jnc .r1
        xor ax, ax
.r1:    mov [rollt], ax
        neg ax
        add ax, ROLL_T          ; elapsed ticks
        mov cl, 10              ; * 65536 / ROLL_T (ROLL_T = 64)
        shl ax, cl
        cmp word [rolldir], 0
        jg .r2
        neg ax
.r2:    add [ents+E_ROLL], ax
.x:     ret

world_advance:
        fld dword [c_speed]
        fmul dword [dtf]
        fst dword [dzf]
        fld st0
        fadd dword [travel]
        fst dword [travel]
        fistp word [dist]
        fadd dword [scroll]
        fcom dword [c_2sp]
        FFLAGS
        jb .s
        fsub dword [c_2sp]
.s:     fstp dword [scroll]
        fwait
        ret

; ---------------------------------------------------------------------
; stage script: dw distance, type, a, b, c
; ---------------------------------------------------------------------
run_script:
.l:     mov si, [scriptp]
        mov ax, [si]
        cmp ax, 0FFFFh
        je .x
        cmp ax, [dist]
        ja .x
        mov bx, [si+2]
        shl bx, 1
        call [ev_table+bx]
        add word [scriptp], 10
        jmp .l
.x:     ret

ev_table: dw ev_text, ev_field, ev_wave, ev_pick, ev_boss

ev_text:
        mov ax, [si+4]
        mov [msg_ptr], ax
        mov ax, [si+6]
        mov [msg_time], ax
        mov bx, [si+8]
        test bx, bx
        jz .x
        mov al, 4
        call sfx_play
.x:     ret

ev_field:
        mov ax, [si+4]
        mov [field_set], ax
        mov ax, [si+6]
        mov [field_gap], ax
        mov ax, [si+8]
        mov [field_spread], ax
        mov ax, [dist]
        mov [field_next], ax
        ret

ev_wave:
        mov ax, [si+4]
        mov cx, [si+6]
        mov dx, [si+8]
        jmp spawn_wave

ev_pick:
        mov ax, [si+4]
        mov dx, [si+6]
        jmp spawn_pick

ev_boss:
        jmp spawn_boss

; ---------------------------------------------------------------------
; entity allocation: SI = cleared free entity, CF=1 if the pool is full
; ---------------------------------------------------------------------
alloc_ent:
        push cx
        push di
        mov si, ents + E_SIZE
        mov cx, NENT - 1
.l:     cmp word [si+E_ACT], 0
        je .f
        add si, E_SIZE
        loop .l
        pop di
        pop cx
        stc
        ret
.f:     mov di, si
        mov cx, E_SIZE / 2
        xor ax, ax
        rep stosw
        mov word [si+E_ACT], 1
        mov word [si+E_RAMP], 0FFh
        pop di
        pop cx
        clc
        ret

; common: place at SPAWN_Z, moving with the ground
%macro WORLD_Z 0
        SETF [si+E_Z], SPAWN_Z
        mov word [si+E_IZ], SPAWN_Z
        fld dword [c_speed]
        fchs
        fstp dword [si+E_VZ]
%endmacro

; random rotation and spin for a floating object (SI)
random_spin:
        call rand
        mov [si+E_YAW], ax
        call rand
        mov [si+E_PITCH], ax
        mov cx, 300
        call rand_mod
        sub ax, 150
        mov [si+E_DYAW], ax
        mov cx, 300
        call rand_mod
        sub ax, 150
        mov [si+E_DPITCH], ax
        ret

; ---------------------------------------------------------------------
; obstacle fields: [field_set] -> dw count, then (model, hp, score)*count
; ---------------------------------------------------------------------
field_tick:
        mov bx, [field_set]
        test bx, bx
        jz .x
        mov ax, [dist]
        cmp ax, [field_next]
        jb .x
        add ax, [field_gap]
        mov [field_next], ax
        call alloc_ent
        jc .x
        mov word [si+E_KIND], K_OBST
        mov cx, [bx]
        call rand_mod
        mov cx, 6
        mul cx
        add bx, ax
        mov ax, [bx+2]
        mov [si+E_MODEL], ax
        mov ax, [bx+4]
        mov [si+E_HP], ax
        mov ax, [bx+6]
        mov [si+E_SCORE], ax
        mov cx, 3
        call rand_mod
        mov [si+E_RAMP], ax
        ; x position: a third of them straight at the player
        mov cx, [field_spread]
        xor ax, ax
        jcxz .setx
        call rand
        and al, 3
        jnz .wide
        mov cx, 50
        call rand_mod
        sub ax, 25
        add ax, [ents+E_IX]
        jmp .setx
.wide:  shl cx, 1
        inc cx
        call rand_mod
        sub ax, [field_spread]
.setx:  mov [si+E_IX], ax
        SETF [si+E_X], ax
        WORLD_Z
        mov bx, [si+E_MODEL]
        test word [bx+M_FLAGS], MF_GROUND
        jz .air
        SETF [si+E_Y], GROUND_I
        mov word [si+E_IY], GROUND_I
        cmp bx, model_archtop   ; gates face the player, no spin
        je .x
        call rand
        mov [si+E_YAW], ax
        jmp .x
.air:   call random_spin
        mov cx, 70
        call rand_mod
        sub ax, 20
        mov [si+E_IY], ax
        SETF [si+E_Y], ax
.x:     fwait
        ret

; ---------------------------------------------------------------------
; enemy waves.  AX = ai type, CX = count, DX = parameter
; ---------------------------------------------------------------------
spawn_wave:
        mov [w_ai], ax
        mov [w_n], cx
        mov [w_p], dx
        mov word [w_i], 0
.l:     mov ax, [w_i]
        cmp ax, [w_n]
        jae .x
        ; k = i - n/2 (signed offset in the formation), |k|
        mov dx, [w_n]
        shr dx, 1
        sub ax, dx
        mov [w_k], ax
        ABS_AX
        mov [w_ak], ax
        call alloc_ent
        jc .x
        mov word [si+E_KIND], K_ENEMY
        mov bx, [w_ai]
        mov [si+E_AI], bx
        shl bx, 1
        call [wave_init+bx]
        fwait
        inc word [w_i]
        jmp .l
.x:     ret

wave_init: dw 0, init_rush, init_swoop, init_hover, init_turret

init_rush:
        mov word [si+E_MODEL], model_dart
        mov word [si+E_RAMP], RAMP_MAGENTA
        mov word [si+E_HP], 1
        mov word [si+E_SCORE], 20
        mov ax, 1
        cmp word [diff], 2
        jb .s
        inc ax
.s:     mov [si+E_SHOTS], ax
        mov ax, [w_k]
        mov cx, 45
        imul cx
        add ax, [w_p]
        mov [si+E_P0], ax
        mov [si+E_IX], ax
        SETF [si+E_X], ax
        mov ax, [w_ak]
        mov cx, 10
        mul cx
        add ax, 8
        mov [si+E_IY], ax
        SETF [si+E_Y], ax
        mov ax, [w_ak]
        mov cx, 70
        mul cx
        add ax, SPAWN_Z
        mov [si+E_IZ], ax
        SETF [si+E_Z], ax
        SETF [si+E_VZ], -600
        mov ax, [w_i]
        mov cl, 13
        shl ax, cl
        mov [si+E_P1], ax
        mov cx, 90
        call rand_mod
        add ax, 20
        mov [si+E_FIRE], ax
        ret

init_swoop:
        mov word [si+E_MODEL], model_dart
        mov word [si+E_RAMP], RAMP_MIXED
        mov word [si+E_HP], 1
        mov word [si+E_SCORE], 30
        mov word [si+E_SHOTS], 1
        mov ax, [w_p]           ; side: +1 enters from the left
        mov cx, -300
        imul cx
        mov [si+E_IX], ax
        SETF [si+E_X], ax
        mov ax, [w_p]
        mov cx, 240
        imul cx
        SETF [si+E_VX], ax
        mov ax, [w_i]
        mov cx, 9
        mul cx
        mov dx, 35
        sub dx, ax
        mov [si+E_IY], dx
        SETF [si+E_Y], dx
        mov ax, [w_i]
        mov cx, 120
        mul cx
        add ax, 700
        mov [si+E_IZ], ax
        SETF [si+E_Z], ax
        SETF [si+E_VZ], -110
        mov ax, [w_p]
        mov cx, -2800h          ; nose into the direction of travel
        imul cx
        mov [si+E_YAW], ax
        mov ax, [w_p]
        mov cx, 1C00h
        imul cx
        mov [si+E_ROLL], ax
        ret

init_hover:
        mov word [si+E_MODEL], model_saucer
        mov word [si+E_RAMP], RAMP_CYAN
        mov word [si+E_HP], 3
        mov word [si+E_SCORE], 60
        mov word [si+E_DYAW], 350
        mov ax, [w_k]
        mov cx, 90
        imul cx
        add ax, [w_p]
        mov [si+E_P2], ax
        mov [si+E_IX], ax
        SETF [si+E_X], ax
        mov ax, [w_ak]
        mov cx, 14
        mul cx
        add ax, 12
        mov [si+E_IY], ax
        SETF [si+E_Y], ax
        mov ax, [w_i]
        mov cx, 90
        mul cx
        add ax, SPAWN_Z
        mov [si+E_IZ], ax
        SETF [si+E_Z], ax
        SETF [si+E_VZ], -650
        mov ax, [w_i]
        mov cx, 55
        mul cx
        add ax, 330
        mov [si+E_P0], ax       ; hover distance
        ret

init_turret:
        mov word [si+E_MODEL], model_turret
        mov word [si+E_RAMP], RAMP_MIXED
        mov word [si+E_HP], 2
        mov word [si+E_SCORE], 40
        mov ax, [w_k]
        mov cx, 120
        imul cx
        add ax, [w_p]
        mov [si+E_IX], ax
        SETF [si+E_X], ax
        SETF [si+E_Y], GROUND_I
        mov word [si+E_IY], GROUND_I
        mov ax, [w_i]
        mov cx, 220
        mul cx
        add ax, SPAWN_Z
        mov [si+E_IZ], ax
        SETF [si+E_Z], ax
        fld dword [c_speed]
        fchs
        fstp dword [si+E_VZ]
        mov cx, 100
        call rand_mod
        mov [si+E_FIRE], ax
        ret

; shield crystal.  AX = x, DX = y
spawn_pick:
        push dx
        push ax
        call alloc_ent
        pop ax
        pop dx
        jc .x
        mov word [si+E_KIND], K_PICK
        mov word [si+E_AI], AI_PICK
        mov word [si+E_MODEL], model_crystal
        mov word [si+E_RAMP], RAMP_CYAN
        mov word [si+E_DYAW], 600
        mov [si+E_IX], ax
        SETF [si+E_X], ax
        mov [si+E_IY], dx
        SETF [si+E_Y], dx
        WORLD_Z
        fwait
.x:     ret

spawn_boss:
        mov word [field_set], 0
        call alloc_ent
        jc .x
        mov [boss_core], si
        mov word [si+E_KIND], K_BOSS
        mov word [si+E_AI], AI_CORE
        mov word [si+E_MODEL], model_core
        mov word [si+E_RAMP], RAMP_MIXED
        mov ax, [diff]
        mov cx, 8
        mul cx
        add ax, 30
        mov [si+E_HP], ax
        mov [boss_maxhp], ax
        mov word [si+E_SCORE], 100
        mov word [si+E_DYAW], 150
        SETF [si+E_Y], 25
        mov word [si+E_IY], 25
        SETF [si+E_Z], SPAWN_Z
        mov word [si+E_IZ], SPAWN_Z
        SETF [si+E_VZ], -450
        mov bx, si
        mov dx, -112
        call .gun
        mov dx, 112
        call .gun
        mov byte [boss_active], 1
        fwait
.x:     ret
.gun:   call alloc_ent
        jc .gx
        mov word [si+E_KIND], K_BOSS
        mov word [si+E_AI], AI_GUN
        mov word [si+E_MODEL], model_gun
        mov word [si+E_RAMP], RAMP_MAGENTA
        mov ax, [diff]
        shl ax, 1
        add ax, 10
        mov [si+E_HP], ax
        add [boss_maxhp], ax
        mov word [si+E_SCORE], 20
        mov [si+E_P0], dx
        mov [si+E_P1], bx
        mov word [si+E_FIRE], 100
        inc word [boss_guns]
        push bx
        call ai_gun             ; place it next to the core
        pop bx
.gx:    ret

; total remaining boss hit points -> AX
boss_hp:
        xor ax, ax
        mov si, ents + E_SIZE
        mov cx, NENT - 1
.l:     cmp word [si+E_ACT], 0
        je .n
        cmp word [si+E_KIND], K_BOSS
        jne .n
        cmp word [si+E_STATE], 0
        jne .n
        add ax, [si+E_HP]
.n:     add si, E_SIZE
        loop .l
        ret

; ---------------------------------------------------------------------
; entity update: AI, movement, despawning
; ---------------------------------------------------------------------
update_entities:
        mov si, ents + E_SIZE
        mov cx, NENT - 1
.l:     cmp word [si+E_ACT], 0
        jx e, .n
        push cx
        mov ax, [dtt]
        add [si+E_T], ax
        sub [si+E_HIT], ax
        jns .h
        mov word [si+E_HIT], 0
.h:     sub [si+E_FIRE], ax
        jns .f
        mov word [si+E_FIRE], 0
.f:     cmp word [si+E_STATE], 0
        je .alive
        sub [si+E_STATE], ax    ; exploding: flash, spin, vanish
        jg .move
        mov word [si+E_ACT], 0
        jmp .next
.alive: mov bx, [si+E_AI]
        shl bx, 1
        call [ai_table+bx]
.move:  call move_entity
        call despawn_check
.next:  pop cx
.n:     add si, E_SIZE
        dec cx
        jx nz, .l
        ret

ai_table: dw ai_none, ai_rush, ai_swoop, ai_hover, ai_turret, ai_core, ai_gun, ai_pick

move_entity:
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
        mov ax, [si+E_KIND]
        cmp ax, K_OBST
        je .zonly
        cmp ax, K_PICK
        je .zonly
        fld dword [si+E_VX]
        fmul dword [dtf]
        fadd dword [si+E_X]
        fstp dword [si+E_X]
        fld dword [si+E_VY]
        fmul dword [dtf]
        fadd dword [si+E_Y]
        fstp dword [si+E_Y]
.zonly: fld dword [si+E_VZ]
        fmul dword [dtf]
        fadd dword [si+E_Z]
        fstp dword [si+E_Z]
        fld dword [si+E_X]
        fistp word [si+E_IX]
        fld dword [si+E_Y]
        fistp word [si+E_IY]
        fld dword [si+E_Z]
        fistp word [si+E_IZ]
        fwait
        ret

despawn_check:
        cmp word [si+E_KIND], K_BOSS
        je .x
        mov ax, [si+E_IZ]
        cmp ax, -120
        jl .kill
        cmp ax, SPAWN_Z + 250
        jg .kill
        mov ax, [si+E_IX]
        ABS_AX
        cmp ax, 700
        jg .kill
        cmp word [si+E_IY], 400
        jg .kill
.x:     ret
.kill:  mov word [si+E_ACT], 0
        ret

; ----- AI routines (SI = entity) -----
ai_none:
        ret

ai_pick:
        mov ax, RAMP_CYAN
        test byte [ticks], 16
        jz .a
        mov ax, RAMP_FLASH
.a:     mov [si+E_RAMP], ax
        ret

; fly straight at the player, weaving, one or two aimed shots
ai_rush:
        mov ax, [si+E_T]
        mov dx, 90
        mul dx
        add ax, [si+E_P1]
        call angptr
        fld dword [bx]
        fmul dword [c_45]
        fiadd word [si+E_P0]
        fstp dword [si+E_X]
        fld dword [bx+256]
        fmul dword [c_bank]
        fistp word [si+E_ROLL]
        fwait
        cmp word [si+E_SHOTS], 0
        je .x
        cmp word [si+E_FIRE], 0
        jne .x
        mov ax, [si+E_IZ]
        cmp ax, 1150
        jg .x
        cmp ax, 350
        jl .x
        dec word [si+E_SHOTS]
        mov ax, 70
        call cd_scaled
        mov [si+E_FIRE], ax
        xor ax, ax
        call fire_bullet
.x:     ret

; cross the screen, shooting once in the middle
ai_swoop:
        cmp word [si+E_SHOTS], 0
        je .x
        mov ax, [si+E_IX]
        ABS_AX
        cmp ax, 70
        jg .x
        dec word [si+E_SHOTS]
        xor ax, ax
        call fire_bullet
.x:     ret

; come in fast, hold position in front of the player, strafe and fire,
; then climb away
ai_hover:
        mov ax, [si+E_P1]
        test ax, ax
        jnz .ph1
        mov ax, [si+E_IZ]
        cmp ax, [si+E_P0]
        jg .x
        fldz
        fstp dword [si+E_VZ]
        mov word [si+E_P1], 1
        mov word [si+E_T], 0
        mov word [si+E_FIRE], 70
        fwait
.x:     ret
.ph1:   cmp ax, 1
        jne .x
        mov ax, [si+E_T]
        mov dx, 70
        mul dx
        call angptr
        fld dword [bx]
        fmul dword [c_70]
        fiadd word [si+E_P2]
        fstp dword [si+E_X]
        fld dword [bx+256]
        fmul dword [c_bank]
        fistp word [si+E_ROLL]
        fwait
        cmp word [si+E_FIRE], 0
        jne .t
        mov ax, 230
        call cd_scaled
        mov [si+E_FIRE], ax
        xor ax, ax
        call fire_bullet
.t:     cmp word [si+E_T], 1100
        jb .x
        mov word [si+E_P1], 2   ; leave
        SETF [si+E_VY], 160
        SETF [si+E_VZ], 420
        fwait
        ret

ai_turret:
        cmp word [si+E_FIRE], 0
        jne .x
        mov ax, [si+E_IZ]
        cmp ax, 1150
        jg .x
        cmp ax, 260
        jl .x
        mov ax, 260
        call cd_scaled
        mov [si+E_FIRE], ax
        mov word [bul_yofs], 22
        xor ax, ax
        call fire_bullet
        mov word [bul_yofs], 0
.x:     ret

; boss core: approach, then sway and fire spreads.  Armoured while
; either arm cannon survives.
ai_core:
        cmp word [si+E_P1], 0
        jne .fight
        cmp word [si+E_IZ], 600
        jg .x
        fldz
        fstp dword [si+E_VZ]
        mov word [si+E_P1], 1
        mov word [si+E_T], 0
        mov word [si+E_FIRE], 150
        fwait
.x:     ret
.fight: mov ax, [si+E_T]
        mov dx, 40
        mul dx
        call angptr
        fld dword [bx]
        fmul dword [c_70]
        fstp dword [si+E_X]
        mov ax, [si+E_T]
        mov dx, 67
        mul dx
        call angptr
        fld dword [bx]
        fmul dword [c_18]
        fadd dword [c_25]
        fstp dword [si+E_Y]
        fwait
        cmp word [si+E_FIRE], 0
        jne .x
        cmp word [boss_guns], 0
        je .rage
        mov ax, 330
        call cd_scaled
        mov [si+E_FIRE], ax
        mov bx, spread3
        mov cx, 3
        jmp fire_spread
.rage:  mov word [si+E_DYAW], 520
        mov ax, 190
        call cd_scaled
        mov [si+E_FIRE], ax
        mov bx, spread5
        mov cx, 5
; BX -> table of lateral velocity offsets, CX = count
fire_spread:
.l:     mov ax, [bx]
        push bx
        push cx
        call fire_bullet
        pop cx
        pop bx
        add bx, 2
        loop .l
        ret

spread3: dw -110, 0, 110
spread5: dw -170, -85, 0, 85, 170

ai_gun:
        mov bx, [si+E_P1]
        fld dword [bx+E_X]
        fiadd word [si+E_P0]
        fstp dword [si+E_X]
        fld dword [bx+E_Y]
        fsub dword [c_8]
        fstp dword [si+E_Y]
        fld dword [bx+E_Z]
        fstp dword [si+E_Z]
        fwait
        cmp word [bx+E_P1], 1   ; core in position?
        jne .x
        cmp word [si+E_FIRE], 0
        jne .x
        mov ax, 200
        call cd_scaled
        mov [si+E_FIRE], ax
        xor ax, ax
        call fire_bullet
.x:     ret

; ---------------------------------------------------------------------
; enemy bullet from SI, aimed at the player.  AX = extra x velocity.
; Direction normalised with the 8087's FSQRT.
; ---------------------------------------------------------------------
fire_bullet:
        push si
        push di
        push cx
        push bx
        mov [boff], ax
        mov di, si
        call alloc_ent
        jc .x
        mov word [si+E_KIND], K_EBUL
        mov ax, [di+E_IX]
        mov [si+E_IX], ax
        mov ax, [di+E_IY]
        mov [si+E_IY], ax
        mov ax, [di+E_IZ]
        mov [si+E_IZ], ax
        fld dword [di+E_X]
        fstp dword [si+E_X]
        fld dword [di+E_Y]
        fiadd word [bul_yofs]
        fstp dword [si+E_Y]
        fld dword [di+E_Z]
        fstp dword [si+E_Z]
        fld dword [ents+E_X]
        fsub dword [si+E_X]     ; dx
        fld dword [ents+E_Y]
        fsub dword [si+E_Y]     ; dy
        fld dword [ents+E_Z]
        fsub dword [si+E_Z]     ; dz          st: dz dy dx
        fld st0
        fmul st0, st0
        fld st2
        fmul st0, st0
        faddp st1, st0
        fld st3
        fmul st0, st0
        faddp st1, st0
        fsqrt                   ; |d|
        fdivr dword [bulspeed]  ; k = speed / |d|
        fmul st1, st0
        fmul st2, st0
        fmulp st3, st0          ; st: dz*k dy*k dx*k
        fstp dword [si+E_VZ]
        fstp dword [si+E_VY]
        fiadd word [boff]
        fstp dword [si+E_VX]
        fwait
        SFX sfx_eshot, 1
.x:     pop bx
        pop cx
        pop di
        pop si
        ret

; ---------------------------------------------------------------------
; player lasers
; ---------------------------------------------------------------------
fire_control:
        mov ax, [cooldown]
        sub ax, [dtt]
        jnc .c
        xor ax, ax
.c:     mov [cooldown], ax
        test ax, ax
        jnz .x
        cmp byte [firing], 0
        je .x
        cmp byte [ship_hidden], 0
        jne .x
        cmp word [nbolt], 8
        jae .x
        mov word [cooldown], 17
        cmp byte [demo], 0
        je .f
        mov word [cooldown], 40
.f:     fld dword [c_10]
        call fire_bolt
        fld dword [c_m10]
        call fire_bolt
        SFX sfx_laser, 1
.x:     ret

; st0 = x offset (popped).  The bolt inherits the ship's sideways motion
; (scaled), which is how you aim.
fire_bolt:
        call alloc_ent
        jnc .ok
        fstp st0
        ret
.ok:    mov word [si+E_KIND], K_BOLT
        fadd dword [ents+E_X]
        fstp dword [si+E_X]
        fld dword [ents+E_Y]
        fsub dword [c_2]
        fstp dword [si+E_Y]
        fld dword [c_boltz]
        fstp dword [si+E_Z]
        fld dword [svx]
        fmul dword [c_aim]
        fstp dword [si+E_VX]
        fld dword [svy]
        fmul dword [c_aim]
        fstp dword [si+E_VY]
        fld dword [c_boltspeed]
        fstp dword [si+E_VZ]
        mov ax, [ents+E_IX]
        mov [si+E_IX], ax
        mov ax, [ents+E_IY]
        mov [si+E_IY], ax
        mov word [si+E_IZ], SHIP_Z + 15
        fwait
        ret

; ---------------------------------------------------------------------
; bomb: clears the screen of fighters and bullets, hurts the boss
; ---------------------------------------------------------------------
do_bomb:
        dec word [bombs]
        mov byte [bgcolor], 15
        mov word [flash], 18
        call set_palette
        SFX sfx_bomb, 5
        mov si, ents + E_SIZE
        mov cx, NENT - 1
.l:     cmp word [si+E_ACT], 0
        je .n
        cmp word [si+E_STATE], 0
        jne .n
        mov ax, [si+E_KIND]
        cmp ax, K_EBUL
        jne .e
        mov word [si+E_ACT], 0
        jmp .n
.e:     cmp ax, K_ENEMY
        jne .b
        push cx
        call destroy
        pop cx
        jmp .n
.b:     cmp ax, K_BOSS
        jne .n
        push cx
        mov cx, 6
.bd:    cmp word [si+E_STATE], 0
        jne .bx
        push cx
        call damage
        pop cx
        loop .bd
.bx:    pop cx
.n:     add si, E_SIZE
        loop .l
        ret

; ---------------------------------------------------------------------
; collisions (integer bounding boxes)
; ---------------------------------------------------------------------
; is entity DI inside target SI's box, padded by (hxpad, hypad, hzpad)?
; CF=1 on hit.  Arches have a gap you can fly (and shoot) through.
box_hit:
        mov bx, [si+E_MODEL]
        mov ax, [di+E_IZ]
        sub ax, [si+E_IZ]
        ABS_AX
        sub ax, [hzpad]
        cmp ax, [bx+M_HZ]
        jg .no
        mov ax, [di+E_IX]
        sub ax, [si+E_IX]
        ABS_AX
        mov [rawdx], ax
        sub ax, [hxpad]
        cmp ax, [bx+M_HX]
        jg .no
        mov ax, [di+E_IY]
        sub ax, [si+E_IY]
        mov [rawdy], ax
        sub ax, [bx+M_CYO]
        ABS_AX
        sub ax, [hypad]
        cmp ax, [bx+M_HY]
        jg .no
        test word [bx+M_FLAGS], MF_ARCH
        jz .yes
        ; inside the arch opening: |dx| < 31 and below the slab (y < 90)
        mov ax, [rawdx]
        add ax, [hxpad]
        cmp ax, 34
        jge .yes
        mov ax, [rawdy]
        add ax, [hypad]
        cmp ax, 84
        jl .no
.yes:   stc
        ret
.no:    clc
        ret

collisions:
        ; list of active bolts
        xor bx, bx
        mov si, ents + E_SIZE
        mov cx, NENT - 1
.bl:    cmp word [si+E_ACT], 0
        je .bn
        cmp word [si+E_KIND], K_BOLT
        jne .bn
        cmp bx, 2 * 10
        jae .bn
        mov [boltlist+bx], si
        add bx, 2
.bn:    add si, E_SIZE
        loop .bl
        shr bx, 1
        mov [nbolt], bx

        mov si, ents + E_SIZE
        mov cx, NENT - 1
.tl:    cmp word [si+E_ACT], 0
        jx e, .tn
        cmp word [si+E_STATE], 0
        jx ne, .tn
        push cx
        mov ax, [si+E_KIND]
        cmp ax, K_EBUL
        je .ship
        cmp ax, K_PICK
        je .ship
        cmp ax, K_BOLT
        je .done
        ; --- player lasers vs this target
        mov word [hxpad], 3
        mov word [hypad], 3
        mov word [hzpad], 20
        xor bp, bp
.bt:    cmp bp, [nbolt]
        jae .ship
        mov bx, bp
        shl bx, 1
        mov di, [boltlist+bx]
        cmp word [di+E_ACT], 0
        je .bnext
        call box_hit
        jnc .bnext
        mov word [di+E_ACT], 0
        push bp
        call damage
        pop bp
        cmp word [si+E_STATE], 0
        jne .done               ; destroyed
.bnext: inc bp
        jmp .bt
        ; --- this entity vs the player ship
.ship:  cmp byte [gstate], GS_PLAY
        jne .done
        cmp byte [ship_hidden], 0
        jne .done
        mov di, ents
        mov ax, [si+E_KIND]
        cmp ax, K_PICK
        je .pick
        cmp word [invuln], 0
        jne .done
        cmp ax, K_EBUL
        je .bul
        mov word [hxpad], 16
        mov word [hypad], 5
        mov word [hzpad], 14
        call box_hit
        jnc .done
        mov ax, 4               ; scenery
        cmp word [si+E_KIND], K_OBST
        je .hu
        mov ax, 6               ; enemy craft
.hu:    call player_hurt
        mov ax, [si+E_KIND]
        cmp ax, K_ENEMY
        je .kill
        cmp ax, K_OBST
        jne .done
        cmp word [si+E_HP], 0
        je .done
.kill:  call destroy
        jmp .done
.bul:   ; bullets: small box around the ship
        mov ax, [si+E_IZ]
        sub ax, SHIP_Z
        ABS_AX
        cmp ax, 18
        jg .done
        mov ax, [si+E_IX]
        sub ax, [ents+E_IX]
        ABS_AX
        cmp ax, 15
        jg .done
        mov ax, [si+E_IY]
        sub ax, [ents+E_IY]
        ABS_AX
        cmp ax, 8
        jg .done
        mov word [si+E_ACT], 0
        cmp word [rollt], 0
        je .hurt
        mov cx, 2               ; rolling: the bullet bounces off
        mov dx, [ramp_tab + (RAMP_FLASH*RAMP_LEVELS + RAMP_LEVELS-1)*2]
        call spawn_particles
        SFX sfx_clink, 2
        jmp .done
.hurt:  mov ax, 3
        call player_hurt
        jmp .done
.pick:  mov word [hxpad], 22
        mov word [hypad], 14
        mov word [hzpad], 24
        call box_hit
        jnc .done
        mov word [si+E_ACT], 0
        add word [shield], 8
        cmp word [shield], SHIELD_MAX
        jbe .pk
        mov word [shield], SHIELD_MAX
.pk:    add word [score], 5
        mov word [msg_ptr], s_shieldup
        mov word [msg_time], 160
        SFX sfx_pickup, 3
.done:  pop cx
.tn:    add si, E_SIZE
        dec cx
        jx nz, .tl
        ret

; a laser hit SI
damage:
        push cx
        push dx
        mov cx, 2
        mov dx, [ramp_tab + (RAMP_FLASH*RAMP_LEVELS + RAMP_LEVELS-1)*2]
        call spawn_particles
        pop dx
        pop cx
        mov ax, [si+E_HP]
        test ax, ax
        jz .armor               ; indestructible scenery
        cmp word [si+E_AI], AI_CORE
        jne .d
        cmp word [boss_guns], 0
        jne .armor
.d:     dec word [si+E_HP]
        jz destroy
        mov word [si+E_HIT], 10
        SFX sfx_hit, 2
        ret
.armor: SFX sfx_clink, 1
        ret

; blow up SI
destroy:
        mov ax, [si+E_SCORE]
        add [score], ax
        mov word [si+E_STATE], 24
        mov word [si+E_DYAW], 900
        mov word [si+E_DPITCH], 600
        push cx
        push dx
        mov cx, 7
        mov bx, [si+E_RAMP]
        cmp bx, RAMP_FLASH
        jb .r
        xor bx, bx
.r:     mov ax, RAMP_LEVELS * 2
        mul bx
        mov bx, ax
        mov dx, [ramp_tab + bx + 2*2]
        call spawn_particles
        pop dx
        pop cx
        SFX sfx_boom, 3
        mov ax, [si+E_AI]
        cmp ax, AI_GUN
        jne .c
        dec word [boss_guns]
        jnz .x
        mov word [msg_ptr], s_core
        mov word [msg_time], 220
.x:     ret
.c:     cmp ax, AI_CORE
        jne .x
        ; boss defeated
        mov word [si+E_STATE], 120
        push cx
        mov cx, 3
.bb:    push cx
        mov cx, 9
        mov dx, [ramp_tab + (RAMP_MAGENTA*RAMP_LEVELS + 3)*2]
        call spawn_particles
        pop cx
        loop .bb
        pop cx
        SFX sfx_bigboom, 6
        mov byte [boss_active], 0
        mov byte [gstate], GS_CLEAR
        mov word [statetimer], 900
        mov ax, [shield]        ; bonus: 50 per shield unit + 500
        mov dx, 5
        mul dx
        add ax, 50
        mov [bonus], ax
        add [score], ax
        ; kill all remaining enemies and bullets
        push si
        mov si, ents + E_SIZE
        mov cx, NENT - 1
.k:     mov ax, [si+E_KIND]
        cmp ax, K_EBUL
        je .kk
        cmp ax, K_ENEMY
        jne .kn
.kk:    mov word [si+E_ACT], 0
.kn:    add si, E_SIZE
        loop .k
        pop si
        ret

player_hurt:
        cmp byte [gstate], GS_PLAY
        jne .x
%ifdef GOD
        xor ax, ax
%endif
        sub [shield], ax
        mov word [invuln], 90
        mov byte [bgcolor], 4
        mov word [flash], 25
        push si
        call set_palette
        pop si
        SFX sfx_hurt, 4
        cmp word [shield], 0
        jg .x
        mov word [shield], 0
        ; ship destroyed
        mov byte [gstate], GS_DEAD
        mov word [statetimer], 380
        mov byte [ship_hidden], 1
        push si
        push cx
        mov si, ents
        mov cx, 10
        mov dx, [ramp_tab + (RAMP_CYAN*RAMP_LEVELS + 3)*2]
        call spawn_particles
        mov cx, 8
        call spawn_particles
        pop cx
        pop si
        SFX sfx_bigboom, 6
.x:     ret

; ---------------------------------------------------------------------
; timers and state transitions
; ---------------------------------------------------------------------
%macro TIMER_DOWN 1
        mov ax, %1
        sub ax, [dtt]
        jnc %%ok
        xor ax, ax
%%ok:   mov %1, ax
%endmacro

state_tick:
        TIMER_DOWN [invuln]
        TIMER_DOWN [rollcd]
        TIMER_DOWN [msg_time]
        cmp word [flash], 0
        je .nf
        TIMER_DOWN [flash]
        cmp word [flash], 0
        jne .nf
        mov byte [bgcolor], 1
        call set_palette
.nf:    TIMER_DOWN [statetimer]
        mov al, [gstate]
        cmp al, GS_TITLE
        jne .s1
        mov al, 39h             ; Space starts a game
        call key_pressed
        jz .x
        jmp new_game
.s1:    cmp word [statetimer], 0
        jne .x
        cmp al, GS_DEAD
        jne .s2
        dec word [lives]
        jz .over
        mov word [shield], SHIELD_MAX
        mov byte [ship_hidden], 0
        mov word [invuln], 220
        mov byte [gstate], GS_PLAY
        ret
.over:  mov byte [gstate], GS_OVER
        mov word [statetimer], 700
        mov ax, [score]
        cmp ax, [hiscore]
        jbe .x
        mov [hiscore], ax
.x:     ret
.s2:    cmp al, GS_OVER
        jne .s3
        jmp game_title
.s3:    cmp al, GS_CLEAR
        jne .x
        inc word [stage]
        cmp word [bombs], 5
        jae .nb
        inc word [bombs]
.nb:    mov byte [gstate], GS_PLAY
        call start_stage
        mov word [invuln], 100
        ret
