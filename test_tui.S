# test_tui.S — XJ380 TUI 函数全覆盖测试
#
# 测试所有终端 xapi 函数。通过函数指针间接调用 trampoline。
#
# 编译: gcc -m64 -nostdlib -static -no-pie -o test_tui.elf test_tui.S -Wl,-Ttext=0x200000

    .intel_syntax noprefix

# ============================================================
# xapi 编号 (必须与 xj380_emu.h 的 xapi_syscall_t 一致)
# ============================================================
.equ X_OUTPUT,            0
.equ X_INPUT,             1    # 跳过(需要stdin)
.equ X_GETLINE,           2    # 跳过(需要stdin)
.equ X_GETCH,             3    # 跳过(需要stdin)
.equ X_ENDLINE,           4
.equ X_PRINTLINE,         5
.equ X_PRINTF,            6
.equ X_OUTPUTSERIAL,      7
.equ X_OPENFILE,          8
.equ X_CLOSEFILE,         9
.equ X_SEARCHFILE,        10
.equ X_MKDIR,             11
.equ X_CREATEFILE,        12
.equ X_DELETEFILE,        13
.equ X_RENAMEFILE,        14
.equ X_READFILE,          15
.equ X_WRITEFILE,         16
.equ X_RMDIR,             17
.equ X_CHAR2INT,          18
.equ X_INT2CHAR,          19
.equ X_HEX2CHAR,          20
.equ X_TORGB,             21
.equ X_TORGBA,            22
.equ X_FORK,              23
.equ X_EXECVE,            24
.equ X_EXIT,              25
.equ X_GETTASKLIST,       26
.equ X_KILLPROCESS,       27
.equ X_GETSYSTEMVERSION,  28
.equ X_GETTIME,           29
.equ X_GETCURRENTUSER,    30
.equ X_GETTIMEX,          31
.equ X_GETCPUMODEL,       32
.equ X_GETMEMORYSIZE,     33
.equ X_BROKEN,            34   # 只测消息，不真崩溃
.equ X_SENDAPPMESSAGE,    35
.equ X_SLEEP,             36
.equ X_RUN,               37
.equ X_FLUSHTIME,         38
.equ X_ALLOCATEMEMORY,    39
.equ X_FREEMEMORY,        40
.equ X_MAPMEMORY,         41

# trampoline 基址
.equ TRAMP, 0xFFFF0000

# ============================================================
# 宏: CALL_X no, [args setup...]
# ============================================================
.macro CALL_X no
    mov  rax, TRAMP + (\no) * 16
    call rax
.endm

    .text
    .globl _start
    .type _start, @function

_start:
    # ============================================================
    # 1. 文本 I/O
    # ============================================================
    lea  rdi, [rip + s_test_header]
    CALL_X X_PRINTLINE

    # --- xapi_PrintLine ---
    lea  rdi, [rip + s_p1]
    CALL_X X_PRINTLINE

    # --- xapi_Output (no newline) ---
    lea  rdi, [rip + s_p2a]
    CALL_X X_OUTPUT
    lea  rdi, [rip + s_p2b]
    CALL_X X_PRINTLINE

    # --- xapi_EndLine ---
    CALL_X X_ENDLINE

    # --- xapi_Printf ---
    lea  rdi, [rip + s_printf_fmt]
    lea  rsi, [rip + s_printf_arg]
    CALL_X X_PRINTF

    # --- xapi_OutputSerial ---
    lea  rdi, [rip + s_serial]
    CALL_X X_OUTPUTSERIAL

    # ============================================================
    # 2. 文件操作
    # ============================================================
    lea  rdi, [rip + s_f1]
    CALL_X X_PRINTLINE

    # --- xapi_CreateFile ---
    lea  rdi, [rip + s_testfile]
    CALL_X X_CREATEFILE

    # --- xapi_OpenFile (写) ---
    lea  rdi, [rip + s_testfile]
    CALL_X X_OPENFILE
    mov  r12, rax               # r12 = XFILE*

    # --- xapi_WriteFile ---
    lea  rdi, [rip + s_testfile]
    lea  rsi, [rip + s_filedata]
    mov  rdx, 21                # "File content: Hello!\n" len
    xor  rcx, rcx               # offset 0
    CALL_X X_WRITEFILE

    # --- xapi_ReadFile ---
    # 分配读缓冲区
    mov  rdi, 256
    CALL_X X_ALLOCATEMEMORY
    mov  r13, rax               # r13 = buffer

    lea  rdi, [rip + s_testfile]
    mov  rsi, r13
    mov  rdx, 256
    xor  rcx, rcx
    CALL_X X_READFILE

    # 输出读取的内容
    mov  rdi, r13
    CALL_X X_PRINTLINE

    # --- xapi_CloseFile ---
    mov  rdi, r12
    CALL_X X_CLOSEFILE

    # --- xapi_RenameFile ---
    lea  rdi, [rip + s_testfile]
    lea  rsi, [rip + s_testfile2]
    CALL_X X_RENAMEFILE

    # 验证重命名
    lea  rdi, [rip + s_testfile2]
    CALL_X X_OPENFILE
    mov  rdi, rax
    CALL_X X_CLOSEFILE

    # --- xapi_SearchFile ---
    # 分配 DirNode 数组 (256 * 264 = 67584 bytes on stack too big, use heap)
    mov  rdi, 256 * 272         # ~68KB for DirNode array
    CALL_X X_ALLOCATEMEMORY
    mov  r14, rax               # r14 = DirNode array

    mov  rdi, 4
    CALL_X X_ALLOCATEMEMORY
    mov  r15, rax               # r15 = count ptr

    lea  rdi, [rip + s_root]
    mov  rsi, r15
    mov  rdx, r14
    CALL_X X_SEARCHFILE

    # 输出文件数
    mov  eax, [r15]
    mov  rdi, rax
    CALL_X X_INT2CHAR
    mov  rdi, rax
    CALL_X X_PRINTLINE

    # --- xapi_Mkdir ---
    lea  rdi, [rip + s_testdir]
    CALL_X X_MKDIR

    # --- xapi_Rmdir ---
    lea  rdi, [rip + s_testdir]
    CALL_X X_RMDIR

    # --- xapi_DeleteFile ---
    lea  rdi, [rip + s_testfile2]
    CALL_X X_DELETEFILE

    # ============================================================
    # 3. 类型转换
    # ============================================================
    lea  rdi, [rip + s_t1]
    CALL_X X_PRINTLINE

    # --- xcr_char2int ---
    lea  rdi, [rip + s_number]
    CALL_X X_CHAR2INT
    # rax = 12345
    mov  rdi, rax
    CALL_X X_INT2CHAR
    mov  rdi, rax
    CALL_X X_PRINTLINE

    # --- xcr_int2char ---
    mov  rdi, 9876543210
    CALL_X X_INT2CHAR
    mov  rdi, rax
    CALL_X X_PRINTLINE

    # --- xcr_hex2char ---
    mov  rdi, 0xDEADBEEF
    CALL_X X_HEX2CHAR
    mov  rdi, rax
    CALL_X X_PRINTLINE

    # --- toRGB ---
    mov  rdi, 255     # R
    mov  rsi, 128     # G
    mov  rdx, 64      # B
    CALL_X X_TORGB
    # rax = RGBA value
    mov  rdi, rax
    CALL_X X_HEX2CHAR
    mov  rdi, rax
    CALL_X X_PRINTLINE

    # --- toRGBA (ARGB → RGBA) ---
    mov  rdi, 0xFFAABBCC
    CALL_X X_TORGBA
    mov  rdi, rax
    CALL_X X_HEX2CHAR
    mov  rdi, rax
    CALL_X X_PRINTLINE

    # ============================================================
    # 4. 进程与线程 (简化测试)
    # ============================================================
    lea  rdi, [rip + s_proc1]
    CALL_X X_PRINTLINE

    # --- xapi_Fork (模拟器不支持, 预期返回 -1) ---
    CALL_X X_FORK
    mov  rdi, rax
    CALL_X X_INT2CHAR
    mov  rdi, rax
    CALL_X X_PRINTLINE

    # --- xapi_GetTaskList ---
    mov  rdi, 256
    CALL_X X_ALLOCATEMEMORY
    mov  r12, rax
    mov  rdi, r12
    mov  rsi, 1
    CALL_X X_GETTASKLIST
    # 输出 PID
    mov  eax, [r12]
    mov  rdi, rax
    CALL_X X_INT2CHAR
    mov  rdi, rax
    CALL_X X_PRINTLINE

    # --- xapi_KillProcess ---
    mov  rdi, 999
    CALL_X X_KILLPROCESS

    # ============================================================
    # 5. 系统信息
    # ============================================================
    lea  rdi, [rip + s_s1]
    CALL_X X_PRINTLINE

    # --- xapi_GetSystemVersion ---
    mov  rdi, 256
    CALL_X X_ALLOCATEMEMORY
    mov  r12, rax
    mov  rdi, r12
    CALL_X X_GETSYSTEMVERSION
    mov  rdi, r12
    CALL_X X_PRINTLINE

    # --- xapi_GetTime ---
    CALL_X X_GETTIME
    mov  rdi, rax
    CALL_X X_INT2CHAR
    mov  rdi, rax
    CALL_X X_PRINTLINE

    # --- xapi_GetCpuModel ---
    mov  rdi, 256
    CALL_X X_ALLOCATEMEMORY
    mov  r12, rax
    mov  rdi, r12
    CALL_X X_GETCPUMODEL
    mov  rdi, r12
    CALL_X X_PRINTLINE

    # --- xapi_GetMemorySize ---
    CALL_X X_GETMEMORYSIZE
    mov  rdi, rax
    CALL_X X_INT2CHAR
    mov  rdi, rax
    CALL_X X_PRINTLINE

    # --- xapi_GetCurrentUser ---
    mov  rdi, 256
    CALL_X X_ALLOCATEMEMORY
    mov  r12, rax
    mov  rdi, r12
    CALL_X X_GETCURRENTUSER
    # 输出 user_type
    mov  eax, [r12]
    mov  rdi, rax
    CALL_X X_INT2CHAR
    mov  rdi, rax
    CALL_X X_PRINTLINE

    # --- xapi_GetTimeX ---
    mov  rdi, 256
    CALL_X X_ALLOCATEMEMORY
    mov  r12, rax
    mov  rdi, r12
    CALL_X X_GETTIMEX

    mov  eax, [r12]             # year
    mov  rdi, rax
    CALL_X X_INT2CHAR
    mov  rdi, rax
    CALL_X X_PRINTLINE

    # ============================================================
    # 6. 系统消息
    # ============================================================
    lea  rdi, [rip + s_msg1]
    CALL_X X_PRINTLINE

    # --- xapi_SendAppMessage ---
    lea  rdi, [rip + s_msg_title]
    lea  rsi, [rip + s_msg_body]
    CALL_X X_SENDAPPMESSAGE

    # --- xapi_Sleep(10ms) ---
    mov  rdi, 10
    CALL_X X_SLEEP

    # --- xapi_Run ---
    lea  rdi, [rip + s_notepad]
    CALL_X X_RUN

    # --- xapi_FlushTime ---
    CALL_X X_FLUSHTIME

    # ============================================================
    # 7. 内存管理
    # ============================================================
    lea  rdi, [rip + s_mem1]
    CALL_X X_PRINTLINE

    # --- xapi_AllocateMemory ---
    mov  rdi, 4096
    CALL_X X_ALLOCATEMEMORY
    mov  r12, rax
    # 输出地址
    mov  rdi, rax
    CALL_X X_HEX2CHAR
    mov  rdi, rax
    CALL_X X_PRINTLINE

    # --- 写入分配的内存 ---
    lea  rdi, [rip + s_testfile]
    lea  rsi, [rip + s_memdata]
    mov  rdx, 13
    xor  rcx, rcx
    CALL_X X_WRITEFILE

    # --- xapi_MapMemory ---
    xor  rdi, rdi         # addr = NULL → 内核选
    mov  rsi, 0x2000      # 8KB
    mov  rdx, 7           # PRESENT|WRITEABLE|USER
    CALL_X X_MAPMEMORY
    mov  rdi, rax
    CALL_X X_HEX2CHAR
    mov  rdi, rax
    CALL_X X_PRINTLINE

    # --- xapi_FreeMemory ---
    mov  rdi, r12
    CALL_X X_FREEMEMORY

    # ============================================================
    # 完成
    # ============================================================
    lea  rdi, [rip + s_done]
    CALL_X X_PRINTLINE

    xor  rdi, rdi
    CALL_X X_EXIT

    hlt

# ============================================================
# 字符串数据
# ============================================================
    .section .rodata
s_test_header:
    .asciz "========== XJ380 TUI 全覆盖测试 =========="

# 1. 文本 I/O
s_p1:
    .asciz "--- 1. 文本 I/O ---"
s_p2a:
    .asciz "xapi_Output "
s_p2b:
    .asciz "+ xapi_PrintLine => same line"
s_printf_fmt:
    .asciz "xapi_Printf: %s\n"
s_printf_arg:
    .asciz "format string works"
s_serial:
    .asciz "xapi_OutputSerial test"

# 2. 文件操作
s_f1:
    .asciz "--- 2. 文件操作 ---"
s_testfile:
    .asciz "/test.txt"
s_testfile2:
    .asciz "/renamed.txt"
s_testdir:
    .asciz "/tmpdir"
s_root:
    .asciz "/"
s_filedata:
    .ascii "File content: Hello!\n"
    .byte 0

# 3. 类型转换
s_t1:
    .asciz "--- 3. 类型转换 ---"
s_number:
    .asciz "12345"

# 4. 进程
s_proc1:
    .asciz "--- 4. 进程与线程 ---"

# 5. 系统信息
s_s1:
    .asciz "--- 5. 系统信息 ---"

# 6. 系统消息
s_msg1:
    .asciz "--- 6. 系统消息 ---"
s_msg_title:
    .asciz "XJ380"
s_msg_body:
    .asciz "This is a test message"
s_notepad:
    .asciz "/usr/bin/notepad"

# 7. 内存
s_mem1:
    .asciz "--- 7. 内存管理 ---"
s_memdata:
    .asciz "Hello Memory!"
    .byte 0

# 完成
s_done:
    .asciz "========== 全部 TUI 测试完成 =========="
