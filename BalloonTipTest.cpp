#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#include <string>
#include <algorithm>
#include <cmath>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
using namespace Gdiplus;
static ULONG_PTR g_gdiplusToken = 0;
static HWND g_hwnd = nullptr;
static HWND g_owner = nullptr;
static UINT_PTR g_timer = 0;
static int g_timeout = 0;
static int g_width = 150;
static int g_height = 105;
static const int SHADOW = 14;
static const int POINTER = 12;
static const int BODY_HEIGHT = 65;
static const int ICON_SIZE = 20;
static const int TEXT_X = 30;
static const int RIGHT_MARGIN = 10;
static std::wstring ToWide(LPCSTR value)
{
    if (!value) return L"";
    int count = MultiByteToWideChar(CP_ACP, 0, value, -1, nullptr, 0);
    if (count <= 1) return L"";
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_ACP, 0, value, -1, &result[0], count);
    result.resize(count - 1);
    return result;
}
static void AddRoundRect(GraphicsPath& path, REAL x, REAL y, REAL w, REAL h, REAL r)
{
    REAL d = r * 2.0f;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
}
static REAL MeasureTextWidth(Graphics& gr, const std::wstring& value, Font& font)
{
    if (value.empty()) return 0.0f;
    REAL maxWidth = 0.0f;
    size_t start = 0;
    while (start <= value.size())
    {
        size_t end = value.find(L'\n', start);
        std::wstring line = value.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        RectF bounds;
        gr.MeasureString(line.c_str(), -1, &font, PointF(0.0f, 0.0f), &bounds);
        maxWidth = std::max(maxWidth, bounds.Width);
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return maxWidth;
}
static void DrawBalloon(HDC screen, int x, int y, LPCSTR titleA, LPCSTR textA)
{
    std::wstring title = ToWide(titleA);
    std::wstring body = ToWide(textA);
    FontFamily tahoma(L"Tahoma");
    Font titleFont(&tahoma, 12.0f, FontStyleBold | FontStyleItalic, UnitPixel);
    Font bodyFont(&tahoma, 12.0f, FontStyleRegular, UnitPixel);
    Bitmap measureBitmap(1, 1, PixelFormat32bppARGB);
    Graphics measureGraphics(&measureBitmap);
    REAL titleWidth = MeasureTextWidth(measureGraphics, title, titleFont);
    REAL bodyWidth = MeasureTextWidth(measureGraphics, body, bodyFont);
    REAL contentWidth = std::max(titleWidth, bodyWidth);
    g_width = std::max(150, static_cast<int>(std::ceil(TEXT_X + contentWidth + RIGHT_MARGIN + SHADOW * 2)));
    g_height = SHADOW * 2 + POINTER + BODY_HEIGHT;
    const int bodyX = SHADOW;
    const int bodyY = SHADOW + POINTER;
    const int bodyW = g_width - SHADOW * 2;
    const int bodyH = BODY_HEIGHT;
    Bitmap bitmap(g_width, g_height, PixelFormat32bppARGB);
    Graphics gr(&bitmap);
    gr.SetSmoothingMode(SmoothingModeAntiAlias);
    gr.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    gr.Clear(Color(0, 0, 0, 0));
    for (int i = 12; i >= 1; --i)
    {
        BYTE alpha = static_cast<BYTE>(2 + (13 - i) * 2);
        GraphicsPath shadowPath;
        AddRoundRect(shadowPath, static_cast<REAL>(bodyX - i / 2 + 2), static_cast<REAL>(bodyY - i / 2 + 5), static_cast<REAL>(bodyW + i), static_cast<REAL>(bodyH + i), 9.0f + i * 0.25f);
        SolidBrush shadowBrush(Color(alpha, 0, 0, 0));
        gr.FillPath(&shadowBrush, &shadowPath);
    }
    PointF pointerPts[3] = {
        PointF(static_cast<REAL>(bodyX + 24), static_cast<REAL>(bodyY + 1)),
        PointF(static_cast<REAL>(bodyX + 39), static_cast<REAL>(bodyY + 1)),
        PointF(static_cast<REAL>(bodyX + 31), static_cast<REAL>(bodyY - POINTER))
    };
    SolidBrush white(Color(255, 255, 255, 255));
    gr.FillPolygon(&white, pointerPts, 3);
    GraphicsPath bodyPath;
    AddRoundRect(bodyPath, static_cast<REAL>(bodyX), static_cast<REAL>(bodyY), static_cast<REAL>(bodyW), static_cast<REAL>(bodyH), 9.0f);
    gr.FillPath(&white, &bodyPath);
    Pen border(Color(255, 20, 20, 20), 1.0f);
    gr.DrawPath(&border, &bodyPath);
    gr.DrawLine(&border, pointerPts[0], pointerPts[2]);
    gr.DrawLine(&border, pointerPts[2], pointerPts[1]);
    PointF tri[3] = {
        PointF(static_cast<REAL>(bodyX + 5), static_cast<REAL>(bodyY + 9)),
        PointF(static_cast<REAL>(bodyX + 25), static_cast<REAL>(bodyY + 9)),
        PointF(static_cast<REAL>(bodyX + 15), static_cast<REAL>(bodyY - 11))
    };
    SolidBrush yellow(Color(255, 255, 205, 0));
    gr.FillPolygon(&yellow, tri, 3);
    Pen iconBorder(Color(255, 150, 105, 0), 1.0f);
    gr.DrawPolygon(&iconBorder, tri, 3);
    FontFamily arial(L"Arial");
    Font exclamation(&arial, 13.0f, FontStyleBold, UnitPixel);
    SolidBrush black(Color(255, 25, 25, 25));
    gr.DrawString(L"!", 1, &exclamation, PointF(static_cast<REAL>(bodyX + 13), static_cast<REAL>(bodyY - 8)), &black);
    SolidBrush titleBrush(Color(255, 0, 0, 0));
    SolidBrush bodyBrush(Color(255, 25, 75, 145));
    RectF bodyRect(static_cast<REAL>(bodyX + TEXT_X), static_cast<REAL>(bodyY + 5), static_cast<REAL>(bodyW - TEXT_X - RIGHT_MARGIN), 33.0f);
    RectF titleRect(static_cast<REAL>(bodyX + TEXT_X), static_cast<REAL>(bodyY + 43), static_cast<REAL>(bodyW - TEXT_X - RIGHT_MARGIN), 17.0f);
    StringFormat fmt;
    fmt.SetTrimming(StringTrimmingEllipsisWord);
    fmt.SetFormatFlags(StringFormatFlagsLineLimit);
    gr.DrawString(body.c_str(), -1, &bodyFont, bodyRect, &fmt, &bodyBrush);
    gr.DrawString(title.c_str(), -1, &titleFont, titleRect, &fmt, &titleBrush);
    POINT dst = { x, y };
    SIZE size = { g_width, g_height };
    POINT src = { 0, 0 };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    HBITMAP hBitmap = nullptr;
    bitmap.GetHBITMAP(Color(0, 0, 0, 0), &hBitmap);
    HDC mem = CreateCompatibleDC(screen);
    HGDIOBJ old = SelectObject(mem, hBitmap);
    UpdateLayeredWindow(g_hwnd, screen, &dst, &size, mem, &src, 0, &blend, ULW_ALPHA);
    SelectObject(mem, old);
    DeleteDC(mem);
    DeleteObject(hBitmap);
}
static LRESULT CALLBACK BalloonProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_TIMER)
    {
        if (g_timer) { KillTimer(hwnd, g_timer); g_timer = 0; }
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    }
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_DESTROY)
    {
        if (g_timer) { KillTimer(hwnd, g_timer); g_timer = 0; }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}
static bool EnsureWindow()
{
    if (g_hwnd && IsWindow(g_hwnd)) return true;
    if (!g_gdiplusToken)
    {
        GdiplusStartupInput input;
        if (GdiplusStartup(&g_gdiplusToken, &input, nullptr) != Ok) return false;
    }
    HINSTANCE inst = GetModuleHandle(nullptr);
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = BalloonProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"VFP_GDIPlus_Balloon_Test";
    RegisterClassEx(&wc);
    g_hwnd = CreateWindowEx(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, wc.lpszClassName, L"", WS_POPUP, 0, 0, g_width, g_height, nullptr, nullptr, inst, nullptr);
    return g_hwnd != nullptr;
}
extern "C" __declspec(dllexport) int WINAPI BT_Show(HWND owner, int x, int y, LPCSTR title, LPCSTR text, int timeoutMs)
{
    if (!EnsureWindow()) return 0;
    g_owner = owner;
    POINT pt = { x, y };
    if (owner && IsWindow(owner)) ClientToScreen(owner, &pt);
    HDC screen = GetDC(nullptr);
    DrawBalloon(screen, pt.x, pt.y, title, text);
    ReleaseDC(nullptr, screen);
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    SetWindowPos(g_hwnd, HWND_TOPMOST, pt.x, pt.y, g_width, g_height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (g_timer) KillTimer(g_hwnd, g_timer);
    g_timeout = timeoutMs;
    if (timeoutMs > 0) g_timer = SetTimer(g_hwnd, 1, static_cast<UINT>(timeoutMs), nullptr);
    return 1;
}
extern "C" __declspec(dllexport) int WINAPI BT_Hide()
{
    if (g_timer && g_hwnd) { KillTimer(g_hwnd, g_timer); g_timer = 0; }
    if (g_hwnd) ShowWindow(g_hwnd, SW_HIDE);
    return 1;
}
extern "C" __declspec(dllexport) int WINAPI BT_Destroy()
{
    BT_Hide();
    if (g_hwnd) { DestroyWindow(g_hwnd); g_hwnd = nullptr; }
    if (g_gdiplusToken) { GdiplusShutdown(g_gdiplusToken); g_gdiplusToken = 0; }
    return 1;
}
extern "C" __declspec(dllexport) int WINAPI BT_Version()
{
    return 101;
}
    
