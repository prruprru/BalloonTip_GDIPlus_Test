#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <propidl.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <string>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

using namespace Gdiplus;

static ULONG_PTR g_gdiplusToken = 0;
static HWND g_hwnd = nullptr;
static HWND g_hwndToolTip = nullptr;
static UINT_PTR g_timer = 0;
static ULONGLONG g_timeoutAt = 0;
static int g_width = 0;
static int g_height = 62;
static int g_maxWidth = 360;
static std::wstring g_fullText;
static bool g_toolTipActive = false;
static HHOOK g_mouseHook = nullptr;
static HHOOK g_keyboardHook = nullptr;
static HINSTANCE g_module = nullptr;
static int g_marginLeft = 14;
static int g_marginTop = 14;
static int g_marginRight = 14;
static int g_marginBottom = 14;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        g_module = hModule;
    return TRUE;
}

static const int BODY_HEIGHT = 15;                        // CAMBIO: antes 34
static const UINT_PTR TIMER_ID = 1;
static const UINT TIMER_INTERVAL = 40;
static const REAL CORNER_RADIUS = 7.0f;                   // CAMBIO: antes 9.0f (diametro 14 <= alto 15)
static const int MAX_MARGIN = 1000;                       // CAMBIO: antes el tope era 64
static const wchar_t* const FONT_NAME = L"Segoe UI";      // CAMBIO: antes Tahoma
static const REAL FONT_SIZE = 12.0f;                      // CAMBIO: 9 pt a 96 DPI, en pixeles (UnitPixel)

static std::wstring ToWide(LPCSTR value)
{
    if (!value) return L"";
    int count = MultiByteToWideChar(CP_ACP, 0, value, -1, nullptr, 0);
    if (count <= 1) return L"";
    std::wstring result(static_cast<size_t>(count), L'\0');
    if (!MultiByteToWideChar(CP_ACP, 0, value, -1, &result[0], count)) return L"";
    result.resize(static_cast<size_t>(count - 1));
    return result;
}

static void AddRoundRect(GraphicsPath& path, REAL x, REAL y, REAL w, REAL h, REAL radius)
{
    REAL d = radius * 2.0f;
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

static double Linearize(BYTE value)
{
    double v = static_cast<double>(value) / 255.0;
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

static double RelativeLuminance(COLORREF color)
{
    return 0.2126 * Linearize(GetRValue(color)) +
           0.7152 * Linearize(GetGValue(color)) +
           0.0722 * Linearize(GetBValue(color));
}

static COLORREF ContrastColor(COLORREF backColor)
{
    double luminance = RelativeLuminance(backColor);
    double contrastBlack = (luminance + 0.05) / 0.05;
    double contrastWhite = 1.05 / (luminance + 0.05);
    return contrastWhite > contrastBlack ? RGB(255, 255, 255) : RGB(0, 0, 0);
}

static COLORREF BorderColor(COLORREF backColor, COLORREF foreColor)
{
    int r = (GetRValue(foreColor) * 75 + GetRValue(backColor) * 25) / 100;
    int g = (GetGValue(foreColor) * 75 + GetGValue(backColor) * 25) / 100;
    int b = (GetBValue(foreColor) * 75 + GetBValue(backColor) * 25) / 100;
    return RGB(r, g, b);
}

static int MeasureTextWidth(const std::wstring& text)
{
    Bitmap measureBitmap(1, 1, PixelFormat32bppARGB);
    Graphics gr(&measureBitmap);
    FontFamily family(FONT_NAME);                                   // CAMBIO
    Font font(&family, FONT_SIZE, FontStyleRegular, UnitPixel);     // CAMBIO
    StringFormat format;
    format.SetFormatFlags(StringFormatFlagsNoWrap);
    RectF bounds(0.0f, 0.0f, 10000.0f, 40.0f);
    RectF measured;
    if (gr.MeasureString(text.c_str(), -1, &font, bounds, &format, &measured) != Ok)
        return 80;
    return static_cast<int>(std::ceil(measured.Width));
}

static void HideToolTip()
{
    if (g_hwndToolTip && IsWindow(g_hwndToolTip))
        SendMessageW(g_hwndToolTip, TTM_POP, 0, 0);
}

static void DestroyToolTip()
{
    if (g_hwndToolTip && IsWindow(g_hwndToolTip))
        DestroyWindow(g_hwndToolTip);
    g_hwndToolTip = nullptr;
    g_toolTipActive = false;
}

static bool CreateToolTip()
{
    bool needToolTip = g_toolTipActive;
    DestroyToolTip();
    g_toolTipActive = needToolTip;
    if (!needToolTip || g_fullText.empty() || !g_hwnd) return true;

    g_hwndToolTip = CreateWindowExW(
        WS_EX_TOPMOST,
        TOOLTIPS_CLASSW,
        nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        g_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

    if (!g_hwndToolTip) return false;

    TOOLINFOW ti = {};
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_SUBCLASS;
    ti.hwnd = g_hwnd;
    ti.uId = 1;
    GetClientRect(g_hwnd, &ti.rect);
    ti.lpszText = const_cast<LPWSTR>(g_fullText.c_str());

    if (!SendMessageW(g_hwndToolTip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti)))
    {
        DestroyToolTip();
        return false;
    }

    SendMessageW(g_hwndToolTip, TTM_SETMAXTIPWIDTH, 0, 420);
    SendMessageW(g_hwndToolTip, TTM_SETDELAYTIME, TTDT_INITIAL, 450);
    SendMessageW(g_hwndToolTip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 10000);
    return true;
}

static void DrawBalloon(HDC screen, int x, int y, COLORREF backColor, COLORREF foreColor, COLORREF borderColor)
{
    const int bodyX = g_marginLeft;
    const int bodyY = g_marginTop;
    const int bodyW = g_width - g_marginLeft - g_marginRight;
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
        AddRoundRect(shadowPath,
            static_cast<REAL>(bodyX - i / 2 + 2),
            static_cast<REAL>(bodyY - i / 2 + 5),
            static_cast<REAL>(bodyW + i),
            static_cast<REAL>(bodyH + i),
            CORNER_RADIUS + i * 0.25f);                              // CAMBIO: antes 9.0f + i * 0.25f
        SolidBrush shadowBrush(Color(alpha, 0, 0, 0));
        gr.FillPath(&shadowBrush, &shadowPath);
    }

    GraphicsPath bodyPath;
    AddRoundRect(bodyPath, static_cast<REAL>(bodyX), static_cast<REAL>(bodyY),
        static_cast<REAL>(bodyW), static_cast<REAL>(bodyH), CORNER_RADIUS);

    SolidBrush background(Color(255, GetRValue(backColor), GetGValue(backColor), GetBValue(backColor)));
    SolidBrush foreground(Color(255, GetRValue(foreColor), GetGValue(foreColor), GetBValue(foreColor)));
    Pen border(Color(255, GetRValue(borderColor), GetGValue(borderColor), GetBValue(borderColor)), 1.0f);

    gr.FillPath(&background, &bodyPath);
    gr.DrawPath(&border, &bodyPath);

    FontFamily family(FONT_NAME);                                   // CAMBIO
    Font font(&family, FONT_SIZE, FontStyleRegular, UnitPixel);     // CAMBIO
    StringFormat format;
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    format.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsNoClip);   // CAMBIO: antes NoWrap | LineLimit
    format.SetAlignment(StringAlignmentNear);
    format.SetLineAlignment(StringAlignmentCenter);

    RectF textRect(
        static_cast<REAL>(bodyX + 12),
        static_cast<REAL>(bodyY),                                    // CAMBIO: antes bodyY + 2
        static_cast<REAL>(bodyW - 24),
        static_cast<REAL>(bodyH));                                   // CAMBIO: antes bodyH - 4

    gr.DrawString(g_fullText.c_str(), -1, &font, textRect, &format, &foreground);

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

static void RemoveInputHooks()
{
    if (g_mouseHook) { UnhookWindowsHookEx(g_mouseHook); g_mouseHook = nullptr; }
    if (g_keyboardHook) { UnhookWindowsHookEx(g_keyboardHook); g_keyboardHook = nullptr; }
}

static void HideBalloonInternal(HWND hwnd)
{
    if (g_timer)
    {
        KillTimer(hwnd, g_timer);
        g_timer = 0;
    }
    g_timeoutAt = 0;
    RemoveInputHooks();
    HideToolTip();
    ShowWindow(hwnd, SW_HIDE);
}

static bool PointInsideWindow(HWND hwnd, POINT pt)
{
    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd)) return false;
    RECT rc;
    GetWindowRect(hwnd, &rc);
    return PtInRect(&rc, pt) != FALSE;
}

static LRESULT CALLBACK LowLevelMouseProc(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && g_hwnd && IsWindowVisible(g_hwnd) &&
        (wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN || wp == WM_MBUTTONDOWN ||
         wp == WM_NCLBUTTONDOWN || wp == WM_NCRBUTTONDOWN || wp == WM_NCMBUTTONDOWN))
    {
        const MSLLHOOKSTRUCT* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp);
        POINT pt = info->pt;
        if (!PointInsideWindow(g_hwnd, pt) && !PointInsideWindow(g_hwndToolTip, pt))
            HideBalloonInternal(g_hwnd);
    }
    return CallNextHookEx(g_mouseHook, code, wp, lp);
}

static LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && g_hwnd && IsWindowVisible(g_hwnd) &&
        (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN))
        HideBalloonInternal(g_hwnd);
    return CallNextHookEx(g_keyboardHook, code, wp, lp);
}

static bool InstallInputHooks()
{
    RemoveInputHooks();
    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, g_module, 0);
    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, g_module, 0);
    if (!g_mouseHook || !g_keyboardHook)
    {
        RemoveInputHooks();
        return false;
    }
    return true;
}

static LRESULT CALLBACK BalloonProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_TIMER && wp == TIMER_ID)
    {
        if (g_timeoutAt && GetTickCount64() >= g_timeoutAt)
            HideBalloonInternal(hwnd);
        return 0;
    }
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (msg == WM_NCHITTEST) return HTCLIENT;
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_DESTROY)
    {
        RemoveInputHooks();
        if (g_timer)
        {
            KillTimer(hwnd, g_timer);
            g_timer = 0;
        }
        g_timeoutAt = 0;
        DestroyToolTip();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static bool EnsureWindow()
{
    if (g_hwnd && IsWindow(g_hwnd)) return true;
    if (!g_module) g_module = GetModuleHandleW(L"BalloonMessage.dll");

    if (!g_gdiplusToken)
    {
        GdiplusStartupInput input;
        if (GdiplusStartup(&g_gdiplusToken, &input, nullptr) != Ok) return false;
    }

    INITCOMMONCONTROLSEX controls = {};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_WIN95_CLASSES;
    if (!InitCommonControlsEx(&controls)) return false;

    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = BalloonProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"VFP_GDIPlus_CompactBalloon";
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    g_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        wc.lpszClassName, L"", WS_POPUP,
        0, 0, 120, g_height,
        nullptr, nullptr, inst, nullptr);

    return g_hwnd != nullptr;
}

extern "C" __declspec(dllexport) int WINAPI BM_ShowEx(
    HWND owner, int x, int y, LPCSTR text, int timeoutMs, COLORREF backColor, int maxWidth,
    int marginLeft, int marginTop, int marginRight, int marginBottom)
{
    if (!EnsureWindow()) return 0;

    g_fullText = ToWide(text);
    if (g_fullText.empty()) return 0;

    g_maxWidth = maxWidth <= 0 ? 360 : maxWidth;
    g_maxWidth = std::max(120, std::min(g_maxWidth, 1200));
    g_marginLeft = std::max(0, std::min(marginLeft, MAX_MARGIN));         // CAMBIO: antes 64
    g_marginTop = std::max(0, std::min(marginTop, MAX_MARGIN));           // CAMBIO: antes 64
    g_marginRight = std::max(0, std::min(marginRight, MAX_MARGIN));       // CAMBIO: antes 64
    g_marginBottom = std::max(0, std::min(marginBottom, MAX_MARGIN));     // CAMBIO: antes 64

    const int measuredWidth = MeasureTextWidth(g_fullText);
    const int textPadding = 24;
    const int horizontalMargins = g_marginLeft + g_marginRight;
    const int maxBodyWidth = std::max(80, g_maxWidth - horizontalMargins);
    const int desiredBodyWidth = measuredWidth + textPadding;
    const int bodyWidth = std::max(80, std::min(desiredBodyWidth, maxBodyWidth));

    g_width = bodyWidth + horizontalMargins;
    g_height = BODY_HEIGHT + g_marginTop + g_marginBottom;
    g_toolTipActive = desiredBodyWidth > maxBodyWidth;

    COLORREF foreColor = ContrastColor(backColor);
    COLORREF borderColor = BorderColor(backColor, foreColor);

    POINT pt = { x, y };
    if (owner && IsWindow(owner)) ClientToScreen(owner, &pt);

    HDC screen = GetDC(nullptr);
    DrawBalloon(screen, pt.x, pt.y, backColor, foreColor, borderColor);
    ReleaseDC(nullptr, screen);

    SetWindowPos(g_hwnd, HWND_TOPMOST, pt.x, pt.y, g_width, g_height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);

    if (!CreateToolTip())
    {
        ShowWindow(g_hwnd, SW_HIDE);
        return 0;
    }

    if (g_timer)
    {
        KillTimer(g_hwnd, g_timer);
        g_timer = 0;
    }

    g_timeoutAt = timeoutMs > 0
        ? GetTickCount64() + static_cast<ULONGLONG>(timeoutMs)
        : 0;

    if (timeoutMs > 0)
    {
        g_timer = SetTimer(g_hwnd, TIMER_ID, TIMER_INTERVAL, nullptr);
        if (!g_timer)
        {
            g_timeoutAt = 0;
            HideToolTip();
            ShowWindow(g_hwnd, SW_HIDE);
            return 0;
        }
    }

    if (!InstallInputHooks())
    {
        HideBalloonInternal(g_hwnd);
        return 0;
    }
    return 1;
}

extern "C" __declspec(dllexport) int WINAPI BM_Hide()
{
    if (g_hwnd && IsWindow(g_hwnd))
        HideBalloonInternal(g_hwnd);
    return 1;
}

extern "C" __declspec(dllexport) int WINAPI BM_Destroy()
{
    BM_Hide();
    if (g_hwnd && IsWindow(g_hwnd))
    {
        DestroyWindow(g_hwnd);
        g_hwnd = nullptr;
    }
    DestroyToolTip();
    UnregisterClassW(L"VFP_GDIPlus_CompactBalloon", GetModuleHandleW(nullptr));   // CAMBIO: linea nueva (opcional)
    if (g_gdiplusToken)
    {
        GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
    return 1;
}

extern "C" __declspec(dllexport) int WINAPI BM_IsVisible()
{
    return g_hwnd && IsWindow(g_hwnd) && IsWindowVisible(g_hwnd) ? 1 : 0;
}

extern "C" __declspec(dllexport) int WINAPI BM_Version()
{
    return 101;                                                       // CAMBIO: antes 100 (para verificar que VFP carga el binario nuevo)
}
