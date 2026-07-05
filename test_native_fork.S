# test_native_fork.S - native fork/wait4 smoke test

    .intel_syntax noprefix

.equ SYS_FORK,        57
.equ SYS_WAIT4,       61
.equ SYS_EXIT,        60
.equ XAPI_FORK,       7389
.equ X_PRINTLINE,     7385

    .text
    .globl _start
_start:
    call sys_fork
    cmp  rax, 0
    je   .child
    cmp  rax, -1
    je   .fail

    mov  r12, rax
    mov  rdi, r12
    lea  rsi, [rip + wait_status]
    xor  edx, edx
    call sys_wait4
    cmp  rax, r12
    jne  .fail
    cmp  dword ptr [rip + wait_status], 0x2a00
    jne  .fail

    call xapi_fork
    cmp  rax, 0
    je   .xapi_child
    cmp  rax, -1
    je   .fail

    mov  r12, rax
    mov  rdi, r12
    lea  rsi, [rip + wait_status]
    xor  edx, edx
    call sys_wait4
    cmp  rax, r12
    jne  .fail
    cmp  dword ptr [rip + wait_status], 0x0700
    jne  .fail

    lea  rdi, [rip + pass_msg]
    call xapi_printline
    xor  edi, edi
    call sys_exit
    hlt

.child:
    mov  edi, 42
    call sys_exit
    hlt

.xapi_child:
    mov  edi, 7
    call sys_exit
    hlt

.fail:
    mov  edi, 2
    call sys_exit
    hlt

sys_fork:
    mov  rdi, SYS_FORK
    xor  esi, esi
    xor  edx, edx
    xor  ecx, ecx
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

xapi_fork:
    mov  rdi, XAPI_FORK
    xor  esi, esi
    xor  edx, edx
    xor  ecx, ecx
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

sys_wait4:
    mov  rcx, rdx
    mov  rdx, rsi
    mov  rsi, rdi
    mov  rdi, SYS_WAIT4
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

sys_exit:
    mov  rsi, rdi
    mov  rdi, SYS_EXIT
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
    .asciz "[native-fork] fork/wait4 smoke passed"

    .data
    .align 4
wait_status:
    .long 0
child_marker:
    .byte 0
