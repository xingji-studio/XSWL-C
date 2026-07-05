# test_native_fork.S - native fork/wait4 smoke test

    .intel_syntax noprefix

.equ SYS_FORK,        57
.equ SYS_WAIT4,       61
.equ SYS_EXIT,        60
.equ SYS_GETGROUPS,   115
.equ XAPI_INPUT,      7382
.equ XAPI_GETCH,      7383
.equ XAPI_FORK,       7389
.equ XAPI_EXECVE,     7390
.equ X_PRINTLINE,     7385
.equ SXAH_CHECK_TERMINAL_INIT_STATUS, 3801
.equ SXAH_INSTALLER_ENUM_DISKS,       128956723895689220
.equ SXAH_INSTALLER_START,            128956723895689221
.equ SXAH_INSTALLER_PROGRESS,         128956723895689222
.equ SXAH_INSTALLER_PRECHECK,         128956723895689223
.equ SXAH_INSTALLER_RESCUE,           128956723895689225
.equ SXAH_INSTALLER_LOG,              128956723895689226

    .text
    .globl _start
_start:
    call sxah_check_terminal_init_status
    cmp  rax, 1
    jne  .fail

    mov  byte ptr [rip + input_buffer], 0x55
    lea  rdi, [rip + input_buffer]
    call xapi_input
    cmp  byte ptr [rip + input_buffer], 0
    jne  .fail

    call xapi_getch
    cmp  rax, 0
    jne  .fail

    mov  dword ptr [rip + groups_buffer], 0x55
    mov  edi, 1
    lea  rsi, [rip + groups_buffer]
    call sys_getgroups
    cmp  rax, 1
    jne  .fail
    cmp  dword ptr [rip + groups_buffer], 0
    jne  .fail

    lea  rdi, [rip + installer_disks]
    call installer_enum_disks
    cmp  rax, 1
    jne  .fail
    cmp  dword ptr [rip + installer_disks], 1
    jne  .fail
    cmp  dword ptr [rip + installer_disks + 12], 512
    jne  .fail
    mov  rax, qword ptr [rip + installer_disks + 16]
    test rax, rax
    je   .fail

    xor  edi, edi
    xor  esi, esi
    lea  rdx, [rip + installer_precheck]
    call installer_precheck_call
    cmp  rax, -30
    jne  .fail
    cmp  dword ptr [rip + installer_precheck + 8], 2
    jne  .fail
    cmp  dword ptr [rip + installer_precheck + 12], 0
    jne  .fail

    xor  edi, edi
    call installer_start
    cmp  rax, -30
    jne  .fail

    lea  rdi, [rip + installer_progress]
    call installer_progress_call
    cmp  rax, 0
    jne  .fail
    cmp  dword ptr [rip + installer_progress], 3
    jne  .fail
    cmp  qword ptr [rip + installer_progress + 8], -30
    jne  .fail

    lea  rdi, [rip + installer_log]
    call installer_log_call
    cmp  rax, 0
    jne  .fail
    cmp  dword ptr [rip + installer_log], 3
    jne  .fail

    mov  edi, 5
    xor  esi, esi
    lea  rdx, [rip + installer_rescue]
    call installer_rescue_call
    cmp  rax, 0
    jne  .fail
    cmp  dword ptr [rip + installer_rescue + 8], 1
    jne  .fail

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

    call xapi_fork
    cmp  rax, 0
    je   .terminal_child
    cmp  rax, -1
    je   .fail

    lea  rdi, [rip + shell_path]
    call xapi_execve
    jmp  .fail

.child:
    mov  edi, 42
    call sys_exit
    hlt

.xapi_child:
    mov  edi, 7
    call sys_exit
    hlt

.terminal_child:
    lea  rdi, [rip + pass_msg]
    call xapi_printline
    xor  edi, edi
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

xapi_execve:
    mov  rsi, rdi
    mov  rdi, XAPI_EXECVE
    xor  edx, edx
    xor  ecx, ecx
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

sxah_check_terminal_init_status:
    mov  rdi, SXAH_CHECK_TERMINAL_INIT_STATUS
    xor  esi, esi
    xor  edx, edx
    xor  ecx, ecx
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

xapi_input:
    mov  rsi, rdi
    mov  rdi, XAPI_INPUT
    xor  edx, edx
    xor  ecx, ecx
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

xapi_getch:
    mov  rdi, XAPI_GETCH
    xor  esi, esi
    xor  edx, edx
    xor  ecx, ecx
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

sys_getgroups:
    mov  rdx, rsi
    mov  rsi, rdi
    mov  rdi, SYS_GETGROUPS
    xor  ecx, ecx
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

installer_enum_disks:
    mov  rsi, rdi
    mov  rdi, SXAH_INSTALLER_ENUM_DISKS
    xor  edx, edx
    xor  ecx, ecx
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

installer_start:
    mov  rsi, rdi
    mov  rdi, SXAH_INSTALLER_START
    xor  edx, edx
    xor  ecx, ecx
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

installer_progress_call:
    mov  rsi, rdi
    mov  rdi, SXAH_INSTALLER_PROGRESS
    xor  edx, edx
    xor  ecx, ecx
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

installer_precheck_call:
    mov  rcx, rdx
    mov  rdx, rsi
    mov  rsi, rdi
    mov  rdi, SXAH_INSTALLER_PRECHECK
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

installer_rescue_call:
    mov  rcx, rdx
    mov  rdx, rsi
    mov  rsi, rdi
    mov  rdi, SXAH_INSTALLER_RESCUE
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

installer_log_call:
    mov  rsi, rdi
    mov  rdi, SXAH_INSTALLER_LOG
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
shell_path:
    .asciz "/apps/system/shell.elf"

    .data
    .align 4
wait_status:
    .long 0
child_marker:
    .byte 0
input_buffer:
    .skip 8
groups_buffer:
    .long 0
installer_disks:
    .skip 1800
installer_precheck:
    .skip 3200
installer_progress:
    .skip 3200
installer_rescue:
    .skip 4300
installer_log:
    .skip 3900
