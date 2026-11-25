; =====================================================================
; stage data: event script, obstacle sets, sound effects, strings
; =====================================================================

EV_TEXT   equ 0                 ; string, ticks, sound
EV_FIELD  equ 1                 ; obstacle set (0 = off), spacing, x spread
EV_WAVE   equ 2                 ; ai, count, parameter
EV_PICK   equ 3                 ; x, y
EV_BOSS   equ 4

%macro EV 5
        dw %1, %2, %3, %4, %5
%endmacro

; distances are world units; the ground moves at 330 units/s
stage_script:
        EV     0, EV_FIELD, set_sparse, 260, 220
        EV  1300, EV_WAVE,  AI_RUSH, 3, 0
        EV  2500, EV_WAVE,  AI_RUSH, 3, -70
        EV  3500, EV_WAVE,  AI_RUSH, 3, 70
        EV  4300, EV_FIELD, set_pillars, 120, 200
        EV  5200, EV_WAVE,  AI_SWOOP, 4, 1
        EV  6600, EV_WAVE,  AI_SWOOP, 4, -1
        EV  7600, EV_PICK,  0, 10, 0
        EV  7900, EV_FIELD, 0, 0, 0
        EV  8200, EV_TEXT,  s_gates, 250, 0
        EV  8400, EV_FIELD, set_gates, 330, 0
        EV 10400, EV_FIELD, set_mixed, 190, 240
        EV 10700, EV_WAVE,  AI_HOVER, 2, 0
        EV 12600, EV_WAVE,  AI_TURRET, 3, 0
        EV 13600, EV_WAVE,  AI_RUSH, 5, 0
        EV 14800, EV_WAVE,  AI_HOVER, 3, 0
        EV 16200, EV_PICK,  -40, 0, 0
        EV 16400, EV_FIELD, set_air, 170, 230
        EV 16800, EV_WAVE,  AI_SWOOP, 5, 1
        EV 18000, EV_WAVE,  AI_SWOOP, 5, -1
        EV 19200, EV_WAVE,  AI_TURRET, 4, 0
        EV 19800, EV_FIELD, set_pillars, 150, 220
        EV 20200, EV_WAVE,  AI_RUSH, 5, -50
        EV 21400, EV_WAVE,  AI_HOVER, 3, 0
        EV 23000, EV_FIELD, set_sparse, 420, 260
        EV 23400, EV_PICK,  0, 10, 0
        EV 24200, EV_TEXT,  s_warning, 500, sfx_alarm
        EV 25400, EV_FIELD, 0, 0, 0
        EV 25800, EV_BOSS,  0, 0, 0
        dw 0FFFFh
TITLE_LOOP equ 24000            ; attract mode restarts before the boss

; obstacle sets: count, then (model, hit points (0 = solid), score/10)
set_sparse:
        dw 5
        dw model_cube, 2, 10,  model_octa, 2, 10,  model_pyramid, 0, 0
        dw model_tetra, 2, 10, model_hexcol, 0, 0
set_pillars:
        dw 3
        dw model_pillar, 0, 0, model_hexcol, 0, 0, model_pillar, 0, 0
set_gates:
        dw 1
        dw model_archtop, 0, 0
set_air:
        dw 4
        dw model_cube, 2, 10, model_octa, 2, 10, model_diamond, 2, 10
        dw model_tetra, 2, 10
set_mixed:
        dw 7
        dw model_cube, 2, 10, model_octa, 2, 10, model_pyramid, 0, 0
        dw model_pillar, 0, 0, model_diamond, 2, 10, model_hexcol, 0, 0
        dw model_tetra, 2, 10

; ---------------------------------------------------------------------
; PC speaker effects: one PIT divisor per 6.9 ms tick, 1 = noise, 0 = end
; ---------------------------------------------------------------------
%define HZ(f) (1193182 / f)
sfx_laser:   dw HZ(2000), HZ(1700), HZ(1450), HZ(1250), HZ(1050), HZ(900), 0
sfx_eshot:   dw HZ(400), HZ(450), HZ(500), HZ(560), 0
sfx_clink:   dw HZ(2600), HZ(2600), HZ(3100), 0
sfx_hit:     dw HZ(300), HZ(250), HZ(300), HZ(250), 0
sfx_boom:    times 20 dw 1
             dw 0
sfx_bigboom: times 70 dw 1
             dw 0
sfx_bomb:    dw HZ(150), HZ(140), HZ(130), HZ(120)
             times 40 dw 1
             dw 0
sfx_hurt:    times 6 dw HZ(110), HZ(85)
             dw 0
sfx_pickup:  times 4 dw HZ(523)
             times 4 dw HZ(659)
             times 4 dw HZ(784)
             times 6 dw HZ(1047)
             dw 0
sfx_roll:    dw 1, 1, HZ(700), HZ(800), HZ(900), HZ(1000), 1, 1, 0
sfx_start:   times 5 dw HZ(392)
             times 5 dw HZ(523)
             times 5 dw HZ(659)
             times 10 dw HZ(784)
             dw 0
sfx_alarm:
%rep 3
             times 16 dw HZ(880)
             times 16 dw HZ(660)
%endrep
             dw 0

; ---------------------------------------------------------------------
; strings
; ---------------------------------------------------------------------
msg_nofpu: db "STORM RUN needs an 8087 (or later) math coprocessor.", 13, 10, "$"
msg_nomem: db "Not enough memory.", 13, 10, "$"
msg_bye:   db "STORM RUN - an 8088 + 8087 flat-shaded 3D shooter.", 13, 10, "$"
s_title:   db "STORM RUN", 0
s_sub:     db "8088 + 8087 FLAT-SHADED 3D", 0
s_press:   db "PRESS SPACE TO START", 0
s_hi:      db "HI-SCORE", 0
s_help1:   db "ARROWS FLY  SPACE FIRE  Z/X ROLL", 0
s_help2:   db "B BOMB  P PAUSE  C COLOR  M SOUND", 0
s_score:   db "SCORE", 0
s_ships:   db "SHIPS", 0
s_bombs:   db "BOMB", 0
s_shield:  db "SHIELD", 0
s_boss:    db "CORE", 0
s_fps:     db "FPS", 0
s_gameover: db "GAME OVER", 0
s_clear:   db "STAGE CLEAR", 0
s_bonus:   db "BONUS", 0
s_paused:  db "PAUSED", 0
s_gates:   db "FLY THROUGH THE GATES", 0
s_warning: db "WARNING - LARGE ENEMY APPROACHING", 0
s_core:    db "CANNONS DOWN - HIT THE CORE", 0
s_shieldup: db "SHIELD UP", 0
s_stage:   db "STAGE "
s_stage_n: db "01", 0
