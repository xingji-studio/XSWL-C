# test_native_smoke.S - native fast-path XAPI syscall patch smoke test

    .intel_syntax noprefix

.equ X_PRINTLINE,       7385
.equ X_EXIT,            60
.equ SYS_READ,          0
.equ SYS_WRITE,         1
.equ SYS_OPEN,          2
.equ SYS_CLOSE,         3
.equ SYS_STAT,          4
.equ SYS_BRK,           12
.equ SYS_FORK,          57
.equ SYS_ARCH_PRCTL,    158
.equ ARCH_SET_FS,       0x1002
.equ ARCH_GET_FS,       0x1003
.equ X_OPEN_FILE,       7387
.equ X_CLOSE_FILE,      7388
.equ X_SEARCH_FILE,     7416
.equ X_READ_FILE,       7423
.equ X_CREATE_WINDOW,   7392
.equ X_SET_ICON,        7395
.equ X_DRAW_POINT,      7396
.equ X_GET_WINDOW_SIZE, 7426
.equ X_DRAW_RECT,       7398
.equ X_DRAW_RECT_FILL,  7399
.equ X_SET_MSG_PRCOR,   7405
.equ X_READ_BUFFER,     7406
.equ X_WRITE_BUFFER,    7407
.equ X_WRITE_BUFFER_A,  7408
.equ X_REFRESH_WINDOW,  7409
.equ X_READ_BUFFER_A,   7417
.equ X_DRAW_PICTURE,    7419
.equ X_GET_CURRENT_USER,7413
.equ X_SLEEP,           7430
.equ X_GET_PIC_SIZE,    7440
.equ X_FLUSH_TIME,      7445
.equ X_OUTPUT_SERIAL,   7427
.equ X_USER_OOBE_REQUIRED, 7452

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

    call sys_fork
    cmp  rax, -1
    jne  .fail

    mov  rdi, qword ptr [rip + native_window_handle]
    lea  rsi, [rip + window_width]
    lea  rdx, [rip + window_height]
    call xapi_get_window_size
    cmp  qword ptr [rip + window_width], 64
    jne  .fail
    cmp  qword ptr [rip + window_height], 48
    jne  .fail

    mov  rdi, qword ptr [rip + native_window_handle]
    lea  rsi, [rip + native_msg_proc]
    call xapi_set_msg_prcor

    mov  rdi, qword ptr [rip + native_window_handle]
    lea  rsi, [rip + icon_path]
    call xapi_set_icon
    test rax, rax
    je   .fail

    mov  rdi, qword ptr [rip + native_window_handle]
    mov  esi, 2
    mov  edx, 3
    mov  ecx, 0xffffffff
    call xapi_draw_point

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

    lea  rdi, [rip + serial_msg]
    call xapi_output_serial

    lea  rdi, [rip + current_user]
    call xapi_get_current_user
    cmp  byte ptr [rip + current_user], 'R'
    jne  .fail

    call xapi_user_oobe_required
    test rax, rax
    jne  .fail

    lea  rdi, [rip + picture_width]
    lea  rsi, [rip + picture_height]
    lea  rdx, [rip + icon_path]
    call xapi_get_pic_size
    cmp  dword ptr [rip + picture_width], 0
    jne  .fail
    cmp  dword ptr [rip + picture_height], 0
    jne  .fail

    mov  byte ptr [rip + pixel_buffer], 0x7f
    mov  rdi, qword ptr [rip + native_window_handle]
    xor  esi, esi
    xor  edx, edx
    mov  ecx, 1
    mov  r8d, 1
    lea  r9, [rip + pixel_buffer]
    call xapi_read_buffer
    cmp  byte ptr [rip + pixel_buffer], 0
    jne  .fail

    lea  rdi, [rip + relative_fixture_path]
    call xapi_open_file
    test rax, rax
    jne  .fail

    lea  rdi, [rip + fixture_path]
    call xapi_open_file
    test rax, rax
    je   .fail
    mov  r13, rax
    cmp  qword ptr [rax], 20
    jne  .fail
    mov  r14, qword ptr [rax + 8]
    test r14, r14
    je   .fail
    cmp  byte ptr [r14], 'n'
    jne  .fail
    cmp  byte ptr [r14 + 12], 'f'
    jne  .fail
    mov  rdi, r13
    call xapi_close_file

    xor  edi, edi
    call xapi_sleep

    lea  rdi, [rip + fixture_path]
    lea  rsi, [rip + read_buffer]
    mov  edx, 6
    xor  ecx, ecx
    call xapi_read_file
    cmp  rax, 6
    jne  .fail
    cmp  byte ptr [rip + read_buffer], 'n'
    jne  .fail
    cmp  byte ptr [rip + read_buffer + 5], 'e'
    jne  .fail

    lea  rdi, [rip + font_dir_path]
    lea  rsi, [rip + search_count]
    lea  rdx, [rip + dir_nodes]
    call xapi_search_file
    cmp  dword ptr [rip + search_count], 0
    je   .fail

    lea  rdi, [rip + fixture_path]
    call sys_open_file
    cmp  rax, -1
    je   .fail
    mov  r15, rax
    mov  rdi, r15
    lea  rsi, [rip + posix_read_buffer]
    mov  edx, 6
    call sys_read_file
    cmp  rax, 6
    jne  .fail
    mov  rdi, r15
    call sys_close_file
    cmp  rax, 0
    jne  .fail

    lea  rdi, [rip + fixture_path]
    lea  rsi, [rip + stat_buffer]
    call sys_stat_file
    cmp  rax, 0
    jne  .fail

    mov  rdi, ARCH_SET_FS
    mov  rsi, 0x12345678
    call sys_arch_prctl
    cmp  rax, 0
    jne  .fail
    mov  rdi, ARCH_GET_FS
    lea  rsi, [rip + fs_value]
    call sys_arch_prctl
    cmp  qword ptr [rip + fs_value], 0x12345678
    jne  .fail

    mov  rdi, 1
    lea  rsi, [rip + write_msg]
    mov  edx, 0
    call sys_write_file
    cmp  rax, 0
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

xapi_open_file:
    mov  rsi, rdi
    mov  rdi, X_OPEN_FILE
    xor  edx, edx
    xor  ecx, ecx
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

xapi_close_file:
    mov  rsi, rdi
    mov  rdi, X_CLOSE_FILE
    xor  edx, edx
    xor  ecx, ecx
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

xapi_read_file:
    mov  r10, rcx
    mov  rcx, rdx
    mov  rdx, rsi
    mov  rsi, rdi
    mov  rdi, X_READ_FILE
    mov  r8, r10
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

xapi_search_file:
    mov  rcx, rdx
    mov  rdx, rsi
    mov  rsi, rdi
    mov  rdi, X_SEARCH_FILE
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

sys_open_file:
    mov  rsi, rdi
    mov  rdi, SYS_OPEN
    xor  edx, edx
    xor  ecx, ecx
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

sys_read_file:
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

sys_close_file:
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

sys_stat_file:
    mov  rdx, rsi
    mov  rsi, rdi
    mov  rdi, SYS_STAT
    xor  ecx, ecx
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

sys_arch_prctl:
    mov  rdx, rsi
    mov  rsi, rdi
    mov  rdi, SYS_ARCH_PRCTL
    xor  ecx, ecx
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

sys_write_file:
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

xapi_get_window_size:
    mov  rcx, rdx
    mov  rdx, rsi
    mov  rsi, rdi
    mov  rdi, X_GET_WINDOW_SIZE
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

xapi_set_icon:
    mov  rdx, rsi
    mov  rsi, rdi
    mov  rdi, X_SET_ICON
    xor  ecx, ecx
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

xapi_draw_point:
    mov  r8, rcx
    mov  rcx, rdx
    mov  rdx, rsi
    mov  rsi, rdi
    mov  rdi, X_DRAW_POINT
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

xapi_read_buffer:
    push r9
    mov  r9, r8
    mov  r8, rcx
    mov  rcx, rdx
    mov  rdx, rsi
    mov  rsi, rdi
    mov  rdi, X_READ_BUFFER
    call enter_syscall
    add  rsp, 8
    ret

xapi_output_serial:
    mov  rsi, rdi
    mov  rdi, X_OUTPUT_SERIAL
    xor  edx, edx
    xor  ecx, ecx
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

xapi_get_current_user:
    mov  rsi, rdi
    mov  rdi, X_GET_CURRENT_USER
    xor  edx, edx
    xor  ecx, ecx
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

xapi_user_oobe_required:
    mov  rdi, X_USER_OOBE_REQUIRED
    xor  esi, esi
    xor  edx, edx
    xor  ecx, ecx
    xor  r8d, r8d
    xor  r9d, r9d
    push 0
    call enter_syscall
    add  rsp, 8
    ret

xapi_get_pic_size:
    mov  rcx, rdx
    mov  rdx, rsi
    mov  rsi, rdi
    mov  rdi, X_GET_PIC_SIZE
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

xapi_sleep:
    mov  rsi, rdi
    mov  rdi, X_SLEEP
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
fixture_path:
    .asciz "/native_fixture.txt"
relative_fixture_path:
    .asciz "native_fixture.txt"
font_dir_path:
    .asciz "/font/ttf"
icon_path:
    .asciz "/graphics/icon/terminal.png"
serial_msg:
    .asciz "native serial smoke"
write_msg:
    .asciz ""

    .data
    .align 8
native_window_handle:
    .quad 0
window_width:
    .quad 0
window_height:
    .quad 0
fs_value:
    .quad 0
search_count:
    .long 0
picture_width:
    .long 1
picture_height:
    .long 1
native_window:
    .long 64
    .long 48
    .quad native_window_title
    .byte 0

    .bss
    .align 8
read_buffer:
    .skip 16
posix_read_buffer:
    .skip 16
pixel_buffer:
    .skip 4
current_user:
    .skip 72
stat_buffer:
    .skip 256
dir_nodes:
    .skip 8192
