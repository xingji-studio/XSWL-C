# test_native_smoke.S - native fast-path XAPI syscall patch smoke test

    .intel_syntax noprefix

.equ X_PRINTLINE,       7385
.equ X_EXIT,            60
.equ SYS_BRK,           12
.equ X_CREATE_WINDOW,   7392
.equ X_DRAW_RECT,       7398
.equ X_DRAW_RECT_FILL,  7399
.equ X_SET_MSG_PRCOR,   7405
.equ X_REFRESH_WINDOW,  7409
.equ X_FLUSH_TIME,      7445

    .text
    .globl _start
_start:
    xor  rdi, rdi
    call native_brk
    test rax, rax
    je   .fail
    mov  r12, rax

    lea  rdi, [rax + 4096]
    call native_brk
    cmp  rax, r12
    jbe  .fail
    mov  byte ptr [r12], 0x5a
    cmp  byte ptr [r12], 0x5a
    jne  .fail

    lea  rdi, [rip + native_window_handle]
    lea  rsi, [rip + native_window]
    call xapi_create_window
    cmp  qword ptr [rip + native_window_handle], 0
    je   .fail

    mov  rdi, qword ptr [rip + native_window_handle]
    lea  rsi, [rip + native_msg_proc]
    call xapi_set_msg_prcor

    mov  rdi, qword ptr [rip + native_window_handle]
    xor  esi, esi
    xor  edx, edx
    mov  ecx, 15
    mov  r8d, 11
    mov  r9d, 0x336699ff
    call xapi_draw_rect_fill

    mov  rdi, qword ptr [rip + native_window_handle]
    mov  esi, 1
    mov  edx, 1
    mov  ecx, 14
    mov  r8d, 10
    mov  r9d, 0xffffffff
    call xapi_draw_rect

    mov  rdi, qword ptr [rip + native_window_handle]
    call xapi_refresh_window
    call xapi_flush_time

    lea  rdi, [rip + pass_msg]
    call xapi_printline

    xor  rdi, rdi
    call xapi_exit
    hlt

.fail:
    mov  rdi, 2
    call xapi_exit
    hlt

native_brk:
    mov  rsi, rdi
    mov  rdi, SYS_BRK
    xor  edx, edx
    xor  ecx, ecx
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

xapi_printline:
    mov  rsi, rdi
    mov  rdi, X_PRINTLINE
    xor  edx, edx
    xor  ecx, ecx
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

xapi_create_window:
    mov  rdx, rsi
    mov  rsi, rdi
    mov  rdi, X_CREATE_WINDOW
    xor  ecx, ecx
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

xapi_set_msg_prcor:
    mov  rdx, rsi
    mov  rsi, rdi
    mov  rdi, X_SET_MSG_PRCOR
    xor  ecx, ecx
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

xapi_draw_rect_fill:
    push r9
    mov  r9, r8
    mov  r8, rcx
    mov  rcx, rdx
    mov  rdx, rsi
    mov  rsi, rdi
    mov  rdi, X_DRAW_RECT_FILL
    call enter_syscall
    add  rsp, 8
    ret

xapi_draw_rect:
    push r9
    mov  r9, r8
    mov  r8, rcx
    mov  rcx, rdx
    mov  rdx, rsi
    mov  rsi, rdi
    mov  rdi, X_DRAW_RECT
    call enter_syscall
    add  rsp, 8
    ret

xapi_refresh_window:
    mov  rsi, rdi
    mov  rdi, X_REFRESH_WINDOW
    xor  edx, edx
    xor  ecx, ecx
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

xapi_flush_time:
    mov  rdi, X_FLUSH_TIME
    xor  esi, esi
    xor  edx, edx
    xor  ecx, ecx
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

xapi_exit:
    mov  rsi, rdi
    mov  rdi, X_EXIT
    xor  edx, edx
    xor  ecx, ecx
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

    .globl enter_syscall
enter_syscall:
    mov  rax, rdi
    mov  r10, r8
    mov  rdi, rsi
    mov  r8, r9
    mov  rsi, rdx
    mov  r9, qword ptr [rsp + 8]
    mov  rdx, rcx
    syscall
    ret

    .section .rodata
native_msg_proc:
    ret
pass_msg:
    .asciz "[native-smoke] patched enter_syscall executed"
native_window_title:
    .asciz "native smoke"

    .data
    .align 8
native_window_handle:
    .quad 0
native_window:
    .long 64
    .long 48
    .quad native_window_title
    .byte 0
