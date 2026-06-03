/*
 * test_unicorn.c — 验证 Unicorn Engine C API 是否正常工作
 *
 * 在内存中放入一段 x86_64 机器码，用 Unicorn 跑一遍。
 *
 * 机器码:
 *   mov  rax, 0x12345678
 *   add  rax, 0x10
 *   ; hook 在第2条指令后调 uc_emu_stop() 停
 *
 * 预期：rax = 0x12345688
 */

#include <stdio.h>
#include <string.h>
#include <unicorn/unicorn.h>

/* x86_64 代码 */
static const uint8_t X86_CODE[] = {
    0x48, 0xC7, 0xC0, 0x78, 0x56, 0x34, 0x12,   /* mov rax, 0x12345678  (7 bytes) */
    0x48, 0x05, 0x10, 0x00, 0x00, 0x00,           /* add rax, 0x10        (6 bytes) */
    0xEB, 0xFE                                    /* jmp $               (2 bytes)  — 保底: 原地跳转 */
};

#define CODE_ADDR  0x1000000
#define CODE_SIZE  (2 * 1024 * 1024)

static uint64_t hit_count = 0;

/* 指令 hook */
static void hook_code(uc_engine *uc, uint64_t addr, uint32_t size, void *user_data)
{
    (void)size;
    (void)user_data;

    hit_count++;
    printf("  [hook #%llu] 0x%llx  size=%u\n",
           (unsigned long long)hit_count,
           (unsigned long long)addr, size);

    /* add 指令在 offset 7 处，长度 6 字节。
     * 执行完 add 后 PC 在 0x100000d (7+6)，此时可以停 */
    if (addr == CODE_ADDR + 7 + 6) {
        printf("  → 在此处停止模拟\n");
        uc_emu_stop(uc);
    }
}

int main(void)
{
    uc_engine    *uc;
    uc_err        err;
    uc_hook       hook;
    uint64_t      rax_val = 0;
    int           ret     = 1;

    printf("=== Unicorn Engine C API 测试 ===\n\n");

    /* ---- 1. 初始化引擎 ---- */
    err = uc_open(UC_ARCH_X86, UC_MODE_64, &uc);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "❌ uc_open() 失败: %s\n", uc_strerror(err));
        return 1;
    }
    printf("✅ uc_open()         — x86_64 引擎初始化\n");

    /* ---- 2. 映射代码页 ---- */
    err = uc_mem_map(uc, CODE_ADDR, CODE_SIZE, UC_PROT_ALL);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "❌ uc_mem_map(CODE) 失败: %s\n", uc_strerror(err));
        goto cleanup;
    }
    printf("✅ uc_mem_map()      — 0x%x (2MB, RWX)\n", CODE_ADDR);

    /* ---- 3. 写入机器码 ---- */
    err = uc_mem_write(uc, CODE_ADDR, X86_CODE, sizeof(X86_CODE));
    if (err != UC_ERR_OK) {
        fprintf(stderr, "❌ uc_mem_write() 失败: %s\n", uc_strerror(err));
        goto cleanup;
    }
    printf("✅ uc_mem_write()    — %zu 字节\n", sizeof(X86_CODE));

    /* ---- 4. 设 RIP ---- */
    err = uc_reg_write(uc, UC_X86_REG_RIP, &(uint64_t){CODE_ADDR});
    if (err != UC_ERR_OK) {
        fprintf(stderr, "❌ uc_reg_write(RIP) 失败: %s\n", uc_strerror(err));
        goto cleanup;
    }
    printf("✅ uc_reg_write()    — RIP=0x%x\n", CODE_ADDR);

    /* ---- 5. Hook ---- */
    err = uc_hook_add(uc, &hook, UC_HOOK_CODE, hook_code, NULL, 1, 0);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "❌ uc_hook_add() 失败: %s\n", uc_strerror(err));
        goto cleanup;
    }
    printf("✅ uc_hook_add()     — 代码 hook\n");

    /* ---- 6. 跑 ---- */
    printf("\n🚀 开始执行...\n");
    err = uc_emu_start(uc, CODE_ADDR, CODE_ADDR + CODE_SIZE, 0, 0);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "  (uc_emu_start 返回: %s)\n", uc_strerror(err));
    }

    /* ---- 7. 读 RAX ---- */
    err = uc_reg_read(uc, UC_X86_REG_RAX, &rax_val);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "❌ uc_reg_read(RAX) 失败: %s\n", uc_strerror(err));
        goto cleanup;
    }

    printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  RAX = 0x%016llx\n", (unsigned long long)rax_val);
    printf("  预期 = 0x0000000012345688\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

    if (rax_val == 0x12345688) {
        printf("\n✅ 全部通过！Unicorn C API 正常工作。\n");
        ret = 0;
    } else {
        printf("\n❌ 寄存器值不匹配！\n");
        ret = 1;
    }

cleanup:
    uc_close(uc);
    return ret;
}
