/**
 * test_all.cpp — XJ380 API 全覆盖测试
 *
 * 编译: xxcc test_all.cpp -o test_all.epf
 * 运行: ./xj380_emu test_all.epf
 */

#include <xapi.h>
#include <krlibc.h>
#include <string.h>

/* 颜色 */
#define RED    0xFF0000FF
#define GREEN  0x00FF00FF
#define BLUE   0x0000FFFF
#define WHITE  0xFFFFFFFF
#define BLACK  0x000000FF
#define GRAY   0x808080FF

int testsPassed = 0;
int testsFailed = 0;

#define TEST(name) xapi_Printf("  %-40s", name)
#define OK()  do { xapi_PrintLine("✅ OK"); testsPassed++; } while(0)
#define FAIL(msg) do { xapi_Printf("❌ %s\n", msg); testsFailed++; } while(0)
#define SECTION(s) xapi_Printf("\n>>> %s\n", s)

int main(int argc, char* argv[], char* envp[])
{
    (void)argc; (void)argv; (void)envp;

    xapi_PrintLine("╔══════════════════════════════════════════════════╗");
    xapi_PrintLine("║     XJ380 API 全覆盖测试                         ║");
    xapi_PrintLine("╚══════════════════════════════════════════════════╝");

    /* ============================================================
     * 1. 系统信息
     * ============================================================ */
    SECTION("1. 系统信息");

    TEST("GetSystemVersion");
    char ver[64]; xapi_GetSystemVersion(ver);
    if (ver[0]) OK(); else FAIL("返回空");

    TEST("GetCpuModel");
    char cpu[128]; xapi_GetCpuModel(cpu);
    if (cpu[0]) OK(); else FAIL("返回空");

    TEST("GetMemorySize");
    UINT64 mem = xapi_GetMemorySize();
    if (mem > 0) { OK(); } else FAIL("返回0");

    TEST("GetTime");
    UINT64 t = xapi_GetTime();
    if (t > 1000000000ULL) OK(); else FAIL("时间戳异常");

    TEST("GetCurrentUser");
    UserInfo ui; xapi_GetCurrentUser(&ui);
    if (ui.user_type >= 0 && ui.user_type <= 4) OK(); else FAIL("用户类型异常");

    TEST("GetTimeX");
    TimeType tm; xapi_GetTimeX(&tm);
    if (tm.tm_year >= 2024) OK(); else FAIL("年份异常");

    /* ============================================================
     * 2. 文本 I/O
     * ============================================================ */
    SECTION("2. 文本 I/O");

    TEST("Output");
    xapi_Output("  [inline output test]");
    OK();

    TEST("PrintLine");
    xapi_PrintLine("");
    OK();

    TEST("EndLine + PrintLine");
    xapi_EndLine();
    xapi_PrintLine("  [after EndLine]");
    OK();

    TEST("Printf (single arg)");
    xapi_Printf("  → test number: %d\n", 42);
    OK();

    TEST("OutputSerial");
    xapi_OutputSerial("  [serial test message]");
    OK();

    /* ============================================================
     * 3. 字符串/颜色转换
     * ============================================================ */
    SECTION("3. 类型转换");

    TEST("int2char");
    WSTR istr = xcr_int2char(1234567890ULL);
    if (istr) OK(); else FAIL("返回NULL");

    TEST("char2int");
    UINT64 inum = xcr_char2int("98765");
    if (inum == 98765) OK(); else FAIL("转换错误");

    TEST("hex2char");
    WSTR hstr = xcr_hex2char(0xABCDEF);
    if (hstr) OK(); else FAIL("返回NULL");

    TEST("toRGB");
    UINT32 rgb = toRGB(0xAA, 0xBB, 0xCC);
    if (rgb == 0xAABBCCFF) OK(); else FAIL("值不匹配");

    TEST("toRGBA");
    UINT32 rgba = toRGBA(0xFFAABBCC);
    if (rgba == 0xFFCCBBAA) OK(); else FAIL("ARGB→RGBA错误");

    /* ============================================================
     * 4. 文件操作
     * ============================================================ */
    SECTION("4. 文件操作");

    TEST("CreateFile");
    UINT64 cr = xapi_CreateFile("/test_all.txt");
    (void)cr;
    OK();

    TEST("WriteFile");
    const char* data = "XJ380 Test Data Content\nLine2\n";
    xapi_WriteFile("/test_all.txt", (char*)data, strlen(data), 0);
    OK();

    TEST("OpenFile + ReadFile");
    XFILE* xf = xapi_OpenFile("/test_all.txt");
    if (xf && xf->length > 0) {
        char rbuf[256];
        memset(rbuf, 0, sizeof(rbuf));
        xapi_ReadFile("/test_all.txt", rbuf, sizeof(rbuf)-1, 0);
        xapi_CloseFile(xf);
        if (rbuf[0]) OK(); else FAIL("读取内容为空");
    } else FAIL("打开失败");

    TEST("SearchFile");
    UINT32 count = 0;
    DirNode dir[255];
    xapi_SearchFile("/", &count, dir);
    if (count > 0) { xapi_Printf(" = %u files", count); OK(); }
    else FAIL("根目录无文件");

    TEST("Mkdir + Rmdir");
    xapi_Mkdir("/testdir");
    xapi_Rmdir("/testdir");
    OK();

    TEST("RenameFile");
    xapi_RenameFile("/test_all.txt", "/renamed_test.txt");
    XFILE* xf2 = xapi_OpenFile("/renamed_test.txt");
    if (xf2) { xapi_CloseFile(xf2); OK(); }
    else FAIL("重命名后打不开");

    TEST("DeleteFile");
    xapi_DeleteFile("/renamed_test.txt");
    OK();

    /* ============================================================
     * 5. 进程
     * ============================================================ */
    SECTION("5. 进程与线程");

    TEST("Fork (应返回 -1, 模拟器不支持)");
    UINT64 fr = xapi_Fork();
    if (fr == (UINT64)-1) OK(); else FAIL("fork应失败");

    TEST("GetTaskList");
    XapiTaskInfo ti[4];
    UINT64 tcnt = xapi_GetTaskList(ti, 4);
    if (tcnt >= 1) OK(); else FAIL("无进程");

    TEST("KillProcess (NOP)");
    xapi_KillProcess(999);
    OK();

    /* ============================================================
     * 6. 系统消息
     * ============================================================ */
    SECTION("6. 系统消息与服务");

    TEST("SendAppMessage");
    xapi_SendAppMessage("TestTitle", "TestBody");
    OK();

    TEST("Sleep (10ms)");
    xapi_Sleep(10);
    OK();

    TEST("Run");
    xapi_Run("/nonexistent");
    OK();

    TEST("FlushTime");
    xapi_FlushTime();
    OK();

    /* ============================================================
     * 7. 内存管理
     * ============================================================ */
    SECTION("7. 内存管理");

    TEST("AllocateMemory + FreeMemory");
    void* ptr = xapi_AllocateMemory(1024);
    if (ptr) { xapi_FreeMemory(ptr); OK(); }
    else FAIL("分配失败");

    TEST("MapMemory");
    void* mptr = xapi_MapMemory(0, 0x2000, 7);
    if (mptr) { xapi_Printf(" = 0x%llx", (UINT64)mptr); OK(); }
    else FAIL("映射失败");

    /* ============================================================
     * 8. GUI — 窗口
     * ============================================================ */
    SECTION("8. GUI 窗口");

    HDLE hWnd = 0;
    XWINDOW xwin;
    xwin.width  = 640;
    xwin.height = 480;
    xwin.title  = "XJ380 API Full Test";
    xwin.sets   = 0;

    TEST("CreateWindow");
    xapi_CreateWindow(&hWnd, &xwin);
    if (hWnd) OK(); else FAIL("窗口创建失败");

    TEST("SetWindowTitle");
    xapi_SetWindowTitle(hWnd, "XJ380 API Test - Modified");
    OK();

    TEST("SetIcon");
    xapi_SetIcon(hWnd, "/nonexistent.ico");
    OK();

    TEST("GetWindowSize");
    UINT64 ww = 0, wh = 0;
    xapi_GetWindowSize(hWnd, &ww, &wh);
    if (ww == 640 && wh == 480) OK(); else FAIL("尺寸不匹配");

    /* ============================================================
     * 9. GUI — 绘图
     * ============================================================ */
    SECTION("9. GUI 绘图");

    TEST("DrawRect (背景)");
    xapi_DrawRect(hWnd, 0, 0, 640, 480, BLACK, true);
    OK();

    TEST("DrawRect (边框)");
    xapi_DrawRect(hWnd, 10, 10, 200, 80, RED, true);
    xapi_DrawRect(hWnd, 10, 10, 200, 80, WHITE, false);
    OK();

    TEST("DrawLine");
    xapi_DrawLine(hWnd, 220, 10, 350, 80, GREEN);
    OK();

    TEST("DrawPoint");
    for(int i=0;i<10;i++) xapi_DrawPoint(hWnd, 360+i*5, 45, BLUE);
    OK();

    TEST("DrawText");
    xapi_DrawText(hWnd, 10, 100, "DrawText: Hello XJ380!", 14, WHITE);
    OK();

    TEST("DrawTextl + CalcTextWidth");
    UINT32 tw = 0;
    xapi_DrawTextl(hWnd, 10, 130, "DrawTextl with width", 12, GREEN, &tw);
    UINT64 cw = xapi_CalcTextWidth("Test", 12);
    if (tw > 0 && cw > 0) OK(); else FAIL("宽度为0");

    TEST("DrawSWText");
    xapi_DrawSWText(hWnd, 10, 160, "DrawSWText: monospace", WHITE);
    OK();

    /* ============================================================
     * 10. GUI — Framebuffer
     * ============================================================ */
    SECTION("10. Framebuffer");

    TEST("WriteBuffer");
    XCOLOR pc = {255, 0, 0};
    xapi_WriteBuffer(hWnd, 500, 10, 1, 1, &pc);
    OK();

    TEST("ReadBuffer");
    XCOLOR rc = {0,0,0};
    xapi_ReadBuffer(hWnd, 500, 10, 1, 1, &rc);
    if (rc.Red == 255) OK(); else FAIL("回读颜色不匹配");

    TEST("WriteBufferA");
    XCOLORA pca = {0, 255, 0, 128};
    xapi_WriteBufferA(hWnd, 520, 10, 1, 1, &pca);
    OK();

    TEST("ReadBufferA");
    XCOLOR rca2 = {0,0,0};
    xapi_ReadBufferA(hWnd, 520, 10, 1, 1, &rca2);
    if (rca2.Green == 255) OK(); else FAIL("回读不匹配");

    TEST("RefreshWindow");
    xapi_RefreshWindow(hWnd);
    OK();

    TEST("RefreshPartWindow");
    xapi_RefreshPartWindow(hWnd, 0, 0, 200, 200);
    OK();

    /* ============================================================
     * 11. GUI — 控件
     * ============================================================ */
    SECTION("11. GUI 控件");

    TEST("Button");
    xapi_Button(hWnd, 100, 50, 420, "Normal Button");
    OK();

    TEST("ButtonEmp");
    xapi_ButtonEmp(hWnd, 200, 200, 420, "Emphasis Button");
    OK();

    TEST("DeleteButton");
    xapi_DeleteButton(hWnd, 200);
    OK();

    TEST("RegisterRightButtonMenu");
    RightMenuItem rmi[2] = {
        {300, "Menu Item 1"},
        {301, "Menu Item 2"}
    };
    xapi_RegisterRightButtonMenu(hWnd, rmi, 2);
    OK();

    TEST("DeleteRightButtonMenu");
    xapi_DeleteRightButtonMenu(hWnd);
    OK();

    /* ============================================================
     * 12. 最终刷新并保持窗口
     * ============================================================ */
    xapi_DrawRect(hWnd, 0, 440, 640, 480, GRAY, true);
    xapi_DrawText(hWnd, 10, 450, "Tests complete. Window closes in 2 seconds...", 12, WHITE);
    xapi_RefreshWindow(hWnd);
    xapi_Sleep(2000);

    xapi_CloseWindow(hWnd);

    /* ============================================================
     * 结果
     * ============================================================ */
    xapi_PrintLine("\n╔══════════════════════════════════════════════════╗");
    xapi_Printf("║  Passed: %-3d  Failed: %-3d                         ║\n",
                testsPassed, testsFailed);
    xapi_PrintLine("╚══════════════════════════════════════════════════╝");

    return testsFailed;
}
