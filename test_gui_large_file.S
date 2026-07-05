# test_gui_large_file.S - non-native XAPI OpenFile must handle files > 16 MiB

    .intel_syntax noprefix

.equ X_PRINTLINE,       5
.equ X_OPENFILE,        8
.equ X_CLOSEFILE,       9
.equ X_EXIT,            25

.equ TRAMP, 0xFFFF0000
.equ EXPECTED_SIZE, 0x01100000

.macro CALL_X no
    mov  rax, TRAMP + (\no) * 16
    call rax
.endm

    .text
    .globl _start
_start:
    lea  rdi, [rip + large_file_path]
    CALL_X X_OPENFILE
    test rax, rax
    je   .fail

    mov  r12, rax
    cmp  qword ptr [rax], EXPECTED_SIZE
    jne  .fail
    cmp  qword ptr [rax + 8], 0
    je   .fail

    mov  rdi, r12
    CALL_X X_CLOSEFILE

    lea  rdi, [rip + pass_msg]
    CALL_X X_PRINTLINE
    xor  rdi, rdi
    CALL_X X_EXIT
    hlt

.fail:
    lea  rdi, [rip + fail_msg]
    CALL_X X_PRINTLINE
    mov  rdi, 1
    CALL_X X_EXIT
    hlt

    .data
large_file_path:
    .asciz "large_fixture.bin"
pass_msg:
    .asciz "[gui-large-file] opened file larger than legacy VFS window"
fail_msg:
    .asciz "[gui-large-file] failed to open large file"
