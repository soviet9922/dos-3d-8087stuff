; ---------------------------------------------------------------------
; 8087 instruction wrappers.
; On an 8088 the CPU must WAIT before every coprocessor instruction (the
; 8087 cannot accept a new opcode while it is busy).  MASM inserted the
; WAIT automatically; here every mnemonic is shadowed by a macro that
; emits WAIT + the real instruction.  Integer code placed *between* two
; FPU instructions therefore runs in parallel with the first one.  An
; explicit FWAIT is still needed before the CPU reads a result the 8087
; stored to memory.   -DCOUNT also counts executed 8087 instructions.
; ---------------------------------------------------------------------

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

%macro fdivr 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fdivr %1
%endmacro

%macro fdivrp 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fdivrp %1
%endmacro

%macro fxch 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fxch %1
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

%macro fcomp 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fcomp %1
%endmacro

%macro fiadd 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fiadd %1
%endmacro

%macro fisub 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fisub %1
%endmacro

%macro fimul 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fimul %1
%endmacro

%macro fidiv 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fidiv %1
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

%macro fabs 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fabs %1
%endmacro

%macro fldz 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fldz %1
%endmacro

%macro fld1 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fld1 %1
%endmacro

%macro fsqrt 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fsqrt %1
%endmacro

%macro frndint 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  frndint %1
%endmacro

%macro ftst 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  ftst %1
%endmacro

%macro fcompp 0-1+
  wait
%ifdef COUNT
  pushf
  inc word [cs:fpuops]
  popf
%endif
  fcompp %1
%endmacro


; read 8087 condition codes into CPU flags (the 8087 has no FSTSW AX)
%macro FFLAGS 0
  fstsw [fsw]
  fwait
  mov ah, [fsw+1]
  sahf
%endmacro

; long conditional jump (8086 Jcc only reaches +-127 bytes):  jx z, label
%macro jx 2
  j%-1 %%skip
  jmp %2
%%skip:
%endmacro

; |AX| -> AX  (clobbers DX)
%macro ABS_AX 0
        cwd
        xor ax, dx
        sub ax, dx
%endmacro

; dword float [dest] = integer src (via tmpw)
%macro SETF 2
        mov word [tmpw], %2
        fild word [tmpw]
        fstp dword %1
%endmacro
