// Exercise the real Win32 invalidation path. A static WM_PRINT preview cannot
// detect stale pixels left behind when native child windows move during scrolling.
#include "../ui.cpp"
#include <iostream>

namespace splitter {
namespace {
struct WindowRenderingTest {
    static std::vector<DWORD> pixels(HWND window) {
        RECT client{}; GetClientRect(window, &client);
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = client.right; info.bmiHeader.biHeight = -client.bottom;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        HDC source = GetDC(window), memory = CreateCompatibleDC(source);
        void* bits = nullptr;
        HBITMAP bitmap = CreateDIBSection(source, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bitmap) { DeleteDC(memory); ReleaseDC(window, source); throw std::runtime_error("Cannot allocate UI capture."); }
        auto old = SelectObject(memory, bitmap);
        BOOL copied = BitBlt(memory, 0, 0, client.right, client.bottom, source, 0, 0, SRCCOPY);
        GdiFlush();
        const auto* begin = static_cast<DWORD*>(bits);
        std::vector<DWORD> result(begin, begin + static_cast<std::size_t>(client.right) * client.bottom);
        for (auto& pixel : result) pixel &= 0x00ffffff; // GDI's unused alpha byte is unspecified.
        SelectObject(memory, old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(window, source);
        if (!copied) throw std::runtime_error("Cannot capture the test window's client surface.");
        return result;
    }
    static void verify(Window& window, const std::string& scenario) {
        // Process only the regions invalidated by the application, as the message loop does.
        RedrawWindow(window.window_, nullptr, nullptr, RDW_UPDATENOW | RDW_ALLCHILDREN);
        auto actual = pixels(window.window_);
        // A complete repaint at the same scroll offset is the reference image.
        RedrawWindow(window.window_, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        auto expected = pixels(window.window_);
        if (std::adjacent_find(expected.begin(), expected.end(), std::not_equal_to<DWORD>()) == expected.end())
            throw std::runtime_error("UI capture is blank; a Windows desktop is required.");
        if (actual != expected) {
            std::size_t differences = 0;
            for (std::size_t i = 0; i < actual.size(); ++i) differences += actual[i] != expected[i];
            throw std::runtime_error(scenario + ": " + std::to_string(differences) + " stale pixels after layout.");
        }
    }
    static int run() {
        Window window(true); HWND handle = window.create(); KillTimer(handle, 1);
        // Only this test window is shown/captured. No audio, settings or driver operations run.
        SetWindowPos(handle, HWND_TOPMOST, 10, 10, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
        ShowWindow(handle, SW_SHOWNOACTIVATE);
        RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        SetWindowPos(handle, nullptr, work.left + 10, work.top + 10, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        int checks = 0;
        for (UINT dpi : {96u, 144u, 192u}) for (int count : {0, 2, 5}) {
            window.dpi_ = dpi;
            window.snapshot_.routes.clear();
            for (int i = 0; i < count; ++i)
                window.snapshot_.routes.push_back({{"test-" + std::to_string(i), "Bluetooth headphones " + std::to_string(i + 1), 35 + i * 10, true}, RouteState::Playing, {}});
            window.fonts(); window.expanded_ = true; window.update();
            for (int height : {480, 640}) {
                SetWindowPos(handle, nullptr, 0, 0,
                    std::min(window.d(660), static_cast<int>(work.right - work.left - 20)),
                    std::min(window.d(height), static_cast<int>(work.bottom - work.top - 20)),
                    SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                for (int offset : {0, 60, 120, 340, 2000, 1000, 200, 0}) {
                    window.scroll(window.d(offset));
                    verify(window, "dpi=" + std::to_string(dpi) + ", outputs=" + std::to_string(count) + ", offset=" + std::to_string(offset)); ++checks;
                }
                // Expand/collapse, timer refresh and keyboard focus also reposition controls.
                window.expanded_ = false; window.update(); verify(window, "collapse"); ++checks;
                window.expanded_ = true; window.update(); verify(window, "expand"); ++checks;
                SendMessageW(handle, controlFocused, reinterpret_cast<WPARAM>(window.buttons_[aboutId]), 0);
                verify(window, "focus scroll"); ++checks;
                SendMessageW(handle, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
                verify(window, "mouse wheel"); ++checks;
                window.update(); verify(window, "timer refresh"); ++checks;
                window.snapshot_.settings.muted = true; window.update(); verify(window, "master mute"); ++checks;
                window.snapshot_.settings.muted = false; window.update(); verify(window, "master unmute"); ++checks;
                if (!window.rows_.empty()) {
                    window.scroll(window.d(outputTop - 8));
                    HWND slider = window.rows_[0].slider;
                    SetFocus(slider);
                    RECT thumb{}, bounds{};
                    SendMessageW(slider, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumb));
                    GetClientRect(slider, &bounds);
                    SendMessageW(slider, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM((thumb.left + thumb.right) / 2, (thumb.top + thumb.bottom) / 2));
                    for (int step : {1, 3, 6, 9, 7, 4, 2, 0}) {
                        SendMessageW(slider, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(8 + (bounds.right - 16) * step / 10, (thumb.top + thumb.bottom) / 2));
                        verify(window, "drag before timer refresh"); ++checks;
                        for (std::size_t i = 0; i < spectrumBands; ++i) window.snapshot_.spectrum[i] = .4f + .4f * std::sin(static_cast<float>(i + step));
                        SendMessageW(handle, WM_TIMER, 1, 0);
                        verify(window, "drag during timer refresh"); ++checks;
                    }
                    SendMessageW(slider, WM_LBUTTONUP, 0, MAKELPARAM(8, (thumb.top + thumb.bottom) / 2));
                    window.scroll(window.d(340)); verify(window, "scroll after drag"); ++checks;
                }
            }
        }
        DestroyWindow(handle);
        return checks;
    }
};
}
}

int main() {
    using namespace splitter;
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        appInstance = GetModuleHandleW(nullptr);
        if (!FindResourceW(appInstance, MAKEINTRESOURCEW(1), RT_MANIFEST)) throw std::runtime_error("UI tests require the same manifest as the app.");
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES}; InitCommonControlsEx(&controls);
        Gdiplus::GdiplusStartupInput startup; ULONG_PTR token = 0;
        if (Gdiplus::GdiplusStartup(&token, &startup, nullptr) != Gdiplus::Ok) throw std::runtime_error("GDI+ initialization failed.");
        struct Graphics { ULONG_PTR token; ~Graphics() { Gdiplus::GdiplusShutdown(token); } } graphics{token};
        WNDCLASSEXW cls{}; cls.cbSize = sizeof(cls); cls.hInstance = appInstance;
        cls.lpfnWndProc = Window::proc; cls.lpszClassName = L"BtAudioSplitterNative";
        if (!RegisterClassExW(&cls)) throw std::runtime_error("Test window registration failed.");
        auto checks = WindowRenderingTest::run();
        std::cout << "PASS " << checks << " UI repaint comparisons (themed controls; 0/2/5 outputs; 100/150/200% scale; dragging, meters, mute, scroll, resize, focus).\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
