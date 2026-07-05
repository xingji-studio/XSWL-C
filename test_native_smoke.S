# test_native_smoke.S - native fast-path XAPI syscall patch smoke test

    .intel_syntax noprefix

.equ X_PRINTLINE,       7385
.equ X_EXIT,            60
.equ SYS_BRK,           12

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
pass_msg:
    .asciz "[native-smoke] patched enter_syscall executed"
