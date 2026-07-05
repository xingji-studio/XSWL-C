# test_gui_events.S - GUI keyboard/mouse event receive test

    .intel_syntax noprefix

.equ X_PRINTLINE,       5
.equ X_EXIT,            25
.equ X_SLEEP,           36
.equ X_FLUSHTIME,       39
.equ X_SETMSGPRCOR,     40
.equ X_CREATEWINDOW,    44

.equ TRAMP, 0xFFFF0000

.equ MSG_CHAR,          0
.equ MSG_MOVE,          1
.equ MSG_LBUTTON,       2
.equ MSG_ROLLER,        5
.equ MSG_SPCHAR,        7
.equ MSG_KEYUP,         9
.equ MSG_KEYDOWN,       10

.equ SEEN_CHAR,         1
.equ SEEN_SPCHAR,       2
.equ SEEN_MOVE,         4
.equ SEEN_LBUTTON,      8
.equ SEEN_ROLLER,       16
.equ SEEN_KEYDOWN,      32
.equ SEEN_KEYUP,        64
.equ SEEN_ALL,          127

.macro CALL_X no
    mov  rax, TRAMP + (\no) * 16
    call rax
.endm

    .text
    .globl _start
_start:
    lea  rdi, [rip + window_handle]
    lea  rsi, [rip + window_def]
    CALL_X X_CREATEWINDOW

    mov  rdi, [rip + window_handle]
    lea  rsi, [rip + msg_proc]
    CALL_X X_SETMSGPRCOR

    mov  r15, 200
.loop:
    CALL_X X_FLUSHTIME
    mov  rax, [rip + seen_mask]
    cmp  rax, SEEN_ALL
    je   .pass
    mov  rdi, 1
    CALL_X X_SLEEP
    dec  r15
    jnz  .loop

    lea  rdi, [rip + fail_msg]
    CALL_X X_PRINTLINE
    mov  rdi, 1
    CALL_X X_EXIT
    hlt

.pass:
    lea  rdi, [rip + pass_msg]
    CALL_X X_PRINTLINE
    xor  rdi, rdi
    CALL_X X_EXIT
    hlt

msg_proc:
    cmp  rdi, MSG_CHAR
    je   .char
    cmp  rdi, MSG_SPCHAR
    je   .spchar
    cmp  rdi, MSG_MOVE
    je   .move
    cmp  rdi, MSG_LBUTTON
    je   .lbutton
    cmp  rdi, MSG_ROLLER
    je   .roller
    cmp  rdi, MSG_KEYDOWN
    je   .keydown
    cmp  rdi, MSG_KEYUP
    je   .keyup
    ret
.char:
    test rsi, rsi
    jne  .done
    cmp  rdx, 0x41
    jne  .done
    or   qword ptr [rip + seen_mask], SEEN_CHAR
    ret
.spchar:
    test rsi, rsi
    jne  .done
    cmp  rdx, 128
    jne  .done
    or   qword ptr [rip + seen_mask], SEEN_SPCHAR
    ret
.move:
    cmp  rsi, 11
    jne  .done
    cmp  rdx, 22
    jne  .done
    or   qword ptr [rip + seen_mask], SEEN_MOVE
    ret
.lbutton:
    cmp  rsi, 33
    jne  .done
    cmp  rdx, 44
    jne  .done
    or   qword ptr [rip + seen_mask], SEEN_LBUTTON
    ret
.roller:
    cmp  rdx, 1
    jne  .done
    or   qword ptr [rip + seen_mask], SEEN_ROLLER
    ret
.keydown:
    test rsi, rsi
    jne  .done
    cmp  rdx, 0x41
    jne  .done
    or   qword ptr [rip + seen_mask], SEEN_KEYDOWN
    ret
.keyup:
    test rsi, rsi
    jne  .done
    cmp  rdx, 0x41
    jne  .done
    or   qword ptr [rip + seen_mask], SEEN_KEYUP
.done:
    ret

    .data
window_handle:
    .quad 0
seen_mask:
    .quad 0
window_def:
    .long 160
    .long 120
    .quad title
    .byte 0
    .zero 7
title:
    .asciz "GUI Event Test"
pass_msg:
    .asciz "[gui-events] keyboard and mouse messages received"
fail_msg:
    .asciz "[gui-events] missing keyboard or mouse messages"
