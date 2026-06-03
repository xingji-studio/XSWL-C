# test_xj380.S — XJ380 模拟器测试 (纯汇编, 静态 x86_64 ELF)
#
# 将 xapi 目标地址存在本地 .data 段,
# 通过间接 call 跳转到 trampoline 页 (0xFFFF0000+).

    .intel_syntax noprefix
    .text
    .globl _start
    .type _start, @function

_start:
    # r15 = 基址 (用寄存器存远端地址，避免 RIP-relative 偏移溢出)
    mov  r15, 0xFFFF0000

    # xapi_PrintLine("=== XJ380 Emulator Test ===")
    lea  rdi, [rip + msg1]
    mov  rax, r15
    add  rax, 5 * 16            # XAPI_PRINTLINE = 5
    call rax

    # xapi_GetTime()
    mov  rax, r15
    add  rax, 30 * 16           # XAPI_GETTIME = 30
    call rax
    # 返回值在 rax

    # xapi_Int2Char(rax)
    mov  rdi, rax
    mov  rax, r15
    add  rax, 19 * 16           # XAPI_INT2CHAR = 19
    call rax
    mov  r12, rax

    # xapi_Output("Timestamp: ")
    lea  rdi, [rip + msg2]
    mov  rax, r15
    add  rax, 0 * 16            # XAPI_OUTPUT = 0
    call rax

    # xapi_PrintLine(r12)
    mov  rdi, r12
    mov  rax, r15
    add  rax, 5 * 16
    call rax

    # xapi_PrintLine("=== Test Complete ===")
    lea  rdi, [rip + msg3]
    mov  rax, r15
    add  rax, 5 * 16
    call rax

    # xapi_Exit(0)
    xor  rdi, rdi
    mov  rax, r15
    add  rax, 26 * 16           # XAPI_EXIT = 26
    call rax

    hlt

    .section .rodata
msg1:
    .asciz "=== XJ380 Emulator Test ==="
msg2:
    .asciz "Timestamp: "
msg3:
    .asciz "=== Test Complete ==="
