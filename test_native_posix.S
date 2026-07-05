# test_native_posix.S - native POSIX/fb compatibility smoke test

    .intel_syntax noprefix

.equ SYS_READ,      0
.equ SYS_WRITE,     1
.equ SYS_OPEN,      2
.equ SYS_CLOSE,     3
.equ SYS_FSTAT,     5
.equ SYS_LSEEK,     8
.equ SYS_MMAP,      9
.equ SYS_MPROTECT,  10
.equ SYS_MUNMAP,    11
.equ SYS_IOCTL,     16
.equ SYS_WRITEV,    20
.equ SYS_PIPE,      22
.equ SYS_SELECT,    23
.equ SYS_UNLINK,    87
.equ SYS_EXIT,      60
.equ O_RDWR_CREAT_TRUNC, 578
.equ O_RDWR,        2
.equ PROT_READ_WRITE, 3
.equ MAP_PRIVATE_ANON, 34
.equ FBIOGET_VSCREENINFO, 0x4600
.equ FBIOGET_FSCREENINFO, 0x4602
.equ FBIOPAN_DISPLAY,     0x4606

    .text
    .globl _start
_start:
    lea  rdi, [rip + tmp_path]
    mov  esi, O_RDWR_CREAT_TRUNC
    mov  edx, 0644
    call sys_open
    cmp  rax, -1
    je   .fail
    mov  r12, rax

    mov  rdi, r12
    lea  rsi, [rip + payload]
    mov  edx, payload_end - payload
    call sys_write
    cmp  rax, payload_end - payload
    jne  .fail

    mov  rdi, r12
    xor  esi, esi
    xor  edx, edx
    call sys_lseek
    cmp  rax, 0
    jne  .fail

    mov  rdi, r12
    lea  rsi, [rip + stat_buffer]
    call sys_fstat
    cmp  rax, 0
    jne  .fail

    mov  rdi, r12
    lea  rsi, [rip + read_buffer]
    mov  edx, payload_end - payload
    call sys_read
    cmp  rax, payload_end - payload
    jne  .fail
    cmp  byte ptr [rip + read_buffer], 'p'
    jne  .fail

    mov  rdi, r12
    call sys_close
    cmp  rax, 0
    jne  .fail

    lea  rdi, [rip + tmp_path]
    call sys_unlink

    lea  rdi, [rip + pipefds]
    call sys_pipe
    cmp  rax, 0
    jne  .fail

    mov  eax, dword ptr [rip + pipefds + 4]
    mov  rdi, rax
    lea  rsi, [rip + pipe_byte]
    mov  edx, 1
    call sys_write
    cmp  rax, 1
    jne  .fail

    lea  rdi, [rip + readfds]
    call fd_zero
    mov  eax, dword ptr [rip + pipefds]
    lea  rdi, [rip + readfds]
    mov  esi, eax
    call fd_set
    mov  eax, dword ptr [rip + pipefds]
    inc  eax
    mov  rdi, rax
    lea  rsi, [rip + readfds]
    xor  edx, edx
    xor  ecx, ecx
    xor  r8d, r8d
    call sys_select
    cmp  rax, 1
    jne  .fail

    xor  edi, edi
    mov  esi, 4096
    mov  edx, PROT_READ_WRITE
    mov  ecx, MAP_PRIVATE_ANON
    mov  r8, -1
    xor  r9d, r9d
    call sys_mmap
    cmp  rax, -1
    je   .fail
    mov  r13, rax
    mov  byte ptr [r13], 0x42
    mov  rdi, r13
    mov  esi, 4096
    mov  edx, 1
    call sys_mprotect
    cmp  rax, 0
    jne  .fail
    mov  rdi, r13
    mov  esi, 4096
    call sys_munmap
    cmp  rax, 0
    jne  .fail

    lea  rdi, [rip + fb_path]
    mov  esi, O_RDWR
    xor  edx, edx
    call sys_open
    cmp  rax, -1
    je   .fail
    mov  r14, rax

    mov  rdi, r14
    mov  esi, FBIOGET_FSCREENINFO
    lea  rdx, [rip + fb_fix]
    call sys_ioctl
    cmp  rax, 0
    jne  .fail
    cmp  dword ptr [rip + fb_fix + 24], 0
    je   .fail

    mov  rdi, r14
    mov  esi, FBIOGET_VSCREENINFO
    lea  rdx, [rip + fb_var]
    call sys_ioctl
    cmp  rax, 0
    jne  .fail
    cmp  dword ptr [rip + fb_var], 640
    jne  .fail

    xor  edi, edi
    mov  esi, 4096
    mov  edx, PROT_READ_WRITE
    mov  ecx, 1
    mov  r8, r14
    xor  r9d, r9d
    call sys_mmap
    cmp  rax, -1
    je   .fail
    mov  r13, rax
    mov  byte ptr [r13], 0x7f
    mov  rdi, r13
    mov  esi, 4096
    call sys_munmap
    cmp  rax, 0
    jne  .fail

    mov  rdi, r14
    mov  esi, FBIOPAN_DISPLAY
    lea  rdx, [rip + fb_var]
    call sys_ioctl
    cmp  rax, 0
    jne  .fail

    xor  edi, edi
    call sys_exit

.fail:
    mov  edi, 2
    call sys_exit

sys_open:
    mov  rcx, rdx
    mov  rdx, rsi
    mov  rsi, rdi
    mov  rdi, SYS_OPEN
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

sys_read:
    mov  rcx, rdx
    mov  rdx, rsi
    mov  rsi, rdi
    mov  rdi, SYS_READ
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

sys_write:
    mov  rcx, rdx
    mov  rdx, rsi
    mov  rsi, rdi
    mov  rdi, SYS_WRITE
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

sys_close:
    mov  rsi, rdi
    mov  rdi, SYS_CLOSE
    xor  edx, edx
    xor  ecx, ecx
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

sys_fstat:
    mov  rdx, rsi
    mov  rsi, rdi
    mov  rdi, SYS_FSTAT
    xor  ecx, ecx
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

sys_lseek:
    mov  rcx, rdx
    mov  rdx, rsi
    mov  rsi, rdi
    mov  rdi, SYS_LSEEK
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

sys_pipe:
    mov  rsi, rdi
    mov  rdi, SYS_PIPE
    xor  edx, edx
    xor  ecx, ecx
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

sys_select:
    mov  r9, r8
    mov  r8, rcx
    mov  rcx, rdx
    mov  rdx, rsi
    mov  rsi, rdi
    mov  rdi, SYS_SELECT
    push 0
    call enter_syscall
    add  rsp, 8
    ret

sys_mmap:
    mov  r10, r8
    mov  r11, r9
    mov  r9, r11
    mov  r8, rcx
    mov  rcx, rdx
    mov  rdx, rsi
    mov  rsi, rdi
    mov  rdi, SYS_MMAP
    push r9
    mov  r9, r10
    call enter_syscall
    add  rsp, 8
    ret

sys_mprotect:
    mov  rcx, rdx
    mov  rdx, rsi
    mov  rsi, rdi
    mov  rdi, SYS_MPROTECT
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

sys_munmap:
    mov  rdx, rsi
    mov  rsi, rdi
    mov  rdi, SYS_MUNMAP
    xor  ecx, ecx
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

sys_ioctl:
    mov  rcx, rdx
    mov  rdx, rsi
    mov  rsi, rdi
    mov  rdi, SYS_IOCTL
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

sys_unlink:
    mov  rsi, rdi
    mov  rdi, SYS_UNLINK
    xor  edx, edx
    xor  ecx, ecx
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

fd_zero:
    pxor xmm0, xmm0
    movups [rdi], xmm0
    movups [rdi + 16], xmm0
    movups [rdi + 32], xmm0
    movups [rdi + 48], xmm0
    movups [rdi + 64], xmm0
    movups [rdi + 80], xmm0
    movups [rdi + 96], xmm0
    movups [rdi + 112], xmm0
    ret

fd_set:
    mov  eax, esi
    mov  ecx, esi
    shr  eax, 6
    and  ecx, 63
    mov  rdx, 1
    shl  rdx, cl
    or   qword ptr [rdi + rax * 8], rdx
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
tmp_path:
    .asciz "/native-posix.tmp"
fb_path:
    .asciz "/dev/fb0"
payload:
    .ascii "posix smoke"
payload_end:
pipe_byte:
    .byte 'x'

    .data
    .align 8
pipefds:
    .long 0, 0
readfds:
    .skip 128
read_buffer:
    .skip 32
stat_buffer:
    .skip 256
fb_fix:
    .skip 128
fb_var:
    .skip 256
