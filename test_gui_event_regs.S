# test_gui_event_regs.S - GUI callbacks must not clobber interrupted code regs

    .intel_syntax noprefix

.equ X_PRINTLINE,       5
.equ X_EXIT,            25
.equ X_FLUSHTIME,       39
.equ X_SETMSGPRCOR,     40
.equ X_CREATEWINDOW,    44

.equ TRAMP, 0xFFFF0000
.equ MSG_KEYDOWN,       10

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

    mov  rbx, 0x1122334455667788
    mov  r12, 0x123456789abcdef0
    mov  r13, 0x0fedcba987654321
    mov  r14, 0x55aa55aa55aa55aa
    mov  r15, 0xaa55aa55aa55aa55

    mov  r10, 32
.loop:
    CALL_X X_FLUSHTIME
    mov  rax, 0x1122334455667788
    cmp  rbx, rax
    jne  .fail
    mov  rax, 0x123456789abcdef0
    cmp  r12, rax
    jne  .fail
    mov  rax, 0x0fedcba987654321
    cmp  r13, rax
    jne  .fail
    mov  rax, 0x55aa55aa55aa55aa
    cmp  r14, rax
    jne  .fail
    mov  rax, 0xaa55aa55aa55aa55
    cmp  r15, rax
    jne  .fail
    cmp  qword ptr [rip + saw_keydown], 1
    je   .pass
    dec  r10
    jnz  .loop

.fail:
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
    cmp  rdi, MSG_KEYDOWN
    jne  .clobber
    cmp  rdx, 0x41
    jne  .clobber
    mov  qword ptr [rip + saw_keydown], 1
.clobber:
    mov  rax, 0
    mov  rbx, 0
    mov  rcx, 0
    mov  rdx, 0
    mov  rsi, 0
    mov  rdi, 0
    mov  rbp, 0
    mov  r8, 0
    mov  r9, 0
    mov  r10, 0
    mov  r11, 0
    mov  r12, 0
    mov  r13, 0
    mov  r14, 0
    mov  r15, 0
    ret

    .data
window_handle:
    .quad 0
saw_keydown:
    .quad 0
window_def:
    .long 160
    .long 120
    .quad title
    .byte 0
    .zero 7
title:
    .asciz "GUI Event Register Test"
pass_msg:
    .asciz "[gui-event-regs] callback preserved interrupted registers"
fail_msg:
    .asciz "[gui-event-regs] callback clobbered interrupted registers"
