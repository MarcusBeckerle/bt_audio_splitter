#include "ui.hpp"
#include "audio.hpp"
#include "setup.hpp"
#include <commctrl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <windowsx.h>
#include <atomic>
#include <array>
#include <optional>

namespace splitter {
namespace {
constexpr COLORREF background = RGB(244, 246, 248), ink = RGB(28, 36, 49), muted = RGB(105, 117, 133), blue = RGB(40, 102, 224);
constexpr int addId = 10, setupId = 11, driverId = 12, bluetoothId = 13, renameId = 14, soundId = 15, sourceId = 16, aboutId = 17, vendorId = 18;
constexpr UINT setupDone = WM_APP + 1;
constexpr UINT controlFocused = WM_APP + 2;
HINSTANCE appInstance;

std::wstring controlText(HWND control) {
    int length = GetWindowTextLengthW(control); std::wstring text(length + 1, L'\0');
    GetWindowTextW(control, text.data(), length + 1); text.resize(length); return text;
}
void setText(HWND control, const std::wstring& text) { if (controlText(control) != text) SetWindowTextW(control, text.c_str()); }
HFONT font(int size, int weight, UINT dpi) {
    return CreateFontW(-MulDiv(size, dpi, 96), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}
void text(HDC dc, HFONT face, const std::wstring& value, RECT rect, COLORREF color = ink, UINT flags = DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX) {
    auto old = SelectObject(dc, face); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, color);
    DrawTextW(dc, value.c_str(), -1, &rect, flags); SelectObject(dc, old);
}
void rounded(HDC dc, RECT rect, COLORREF color, float radius, COLORREF border = CLR_INVALID) {
    Gdiplus::Graphics g(dc); g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    float x = static_cast<float>(rect.left), y = static_cast<float>(rect.top), w = static_cast<float>(rect.right - rect.left - 1), h = static_cast<float>(rect.bottom - rect.top - 1), r = radius * 2;
    Gdiplus::GraphicsPath path;
    path.AddArc(x, y, r, r, 180, 90); path.AddArc(x + w - r, y, r, r, 270, 90);
    path.AddArc(x + w - r, y + h - r, r, r, 0, 90); path.AddArc(x, y + h - r, r, r, 90, 90); path.CloseFigure();
    auto c = [](COLORREF v) { return Gdiplus::Color(255, GetRValue(v), GetGValue(v), GetBValue(v)); };
    Gdiplus::SolidBrush brush(c(color)); g.FillPath(&brush, &path);
    if (border != CLR_INVALID) { Gdiplus::Pen pen(c(border)); g.DrawPath(&pen, &path); }
}
LRESULT CALLBACK focusProc(HWND control, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (message == WM_SETFOCUS) SendMessageW(GetParent(control), controlFocused, reinterpret_cast<WPARAM>(control), 0);
    return DefSubclassProc(control, message, wp, lp);
}
HWND button(HWND parent, int id, const wchar_t* label) {
    HWND control = CreateWindowExW(0, L"BUTTON", label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 1, 1, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), appInstance, nullptr);
    SetWindowSubclass(control, focusProc, 1, 0); return control;
}
std::wstring status(const RouteView& route) {
    if (!route.error.empty()) return L"Verbindung wird erneut versucht";
    switch (route.state) {
    case RouteState::Paused: return L"Pausiert";
    case RouteState::Blocked: return L"Diese Ausgabe ist nicht geeignet";
    case RouteState::WaitingDevice: return L"Wartet auf Gerät · verbindet automatisch";
    case RouteState::WaitingSource: return L"Wartet auf Audioeingang";
    case RouteState::Playing: return L"Verbunden · Stereo";
    }
    return L"";
}

struct Modal {
    HWND window = nullptr, list = nullptr, accept = nullptr, cancel = nullptr;
    HFONT face = nullptr;
    std::vector<Endpoint> items;
    std::wstring body;
    int chosen = -1;
    bool done = false;
    UINT dpi = 96;
    int d(int value) const { return MulDiv(value, dpi, 96); }
    static LRESULT CALLBACK proc(HWND w, UINT message, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<Modal*>(GetWindowLongPtrW(w, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Modal*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            self->window = w; SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(w, message, wp, lp);
        switch (message) {
        case WM_CREATE: {
            self->dpi = GetDpiForWindow(w); self->face = font(14, FW_NORMAL, self->dpi);
            if (self->body.empty()) {
                self->list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"Audiogeräte", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 0, 0, 1, 1, w, reinterpret_cast<HMENU>(100), appInstance, nullptr);
                ListView_SetExtendedListViewStyle(self->list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
                const wchar_t* headers[] = {L"Windows-Gerät", L"Status", L"Gerätekennung"};
                int widths[] = {285, 105, 340};
                for (int i = 0; i < 3; ++i) { LVCOLUMNW col{}; col.mask = LVCF_TEXT | LVCF_WIDTH; col.pszText = const_cast<wchar_t*>(headers[i]); col.cx = self->d(widths[i]); ListView_InsertColumn(self->list, i, &col); }
                for (std::size_t i = 0; i < self->items.size(); ++i) {
                    auto name = wide(self->items[i].name), id = wide(self->items[i].id);
                    LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = static_cast<int>(i); item.pszText = name.data(); ListView_InsertItem(self->list, &item);
                    ListView_SetItemText(self->list, static_cast<int>(i), 1, const_cast<wchar_t*>(self->items[i].available ? L"Bereit" : L"Offline"));
                    ListView_SetItemText(self->list, static_cast<int>(i), 2, id.data());
                }
                if (!self->items.empty()) ListView_SetItemState(self->list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            } else {
                self->list = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", self->body.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 0, 0, 1, 1, w, nullptr, appInstance, nullptr);
            }
            self->accept = CreateWindowW(L"BUTTON", self->body.empty() ? L"Auswählen" : L"Schließen", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 1, 1, w, reinterpret_cast<HMENU>(IDOK), appInstance, nullptr);
            self->cancel = CreateWindowW(L"BUTTON", L"Abbrechen", WS_CHILD | WS_TABSTOP | (self->body.empty() ? WS_VISIBLE : 0), 0, 0, 1, 1, w, reinterpret_cast<HMENU>(IDCANCEL), appInstance, nullptr);
            for (auto c : {self->list, self->accept, self->cancel}) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(self->face), TRUE);
            return 0;
        }
        case WM_SIZE: {
            int width = LOWORD(lp), height = HIWORD(lp), margin = self->d(16);
            MoveWindow(self->list, margin, margin, width - 2 * margin, height - self->d(80), TRUE);
            MoveWindow(self->accept, width - self->d(240), height - self->d(48), self->d(105), self->d(32), TRUE);
            MoveWindow(self->cancel, width - self->d(125), height - self->d(48), self->d(105), self->d(32), TRUE); return 0;
        }
        case WM_NOTIFY:
            if (reinterpret_cast<NMHDR*>(lp)->code == NM_DBLCLK) SendMessageW(w, WM_COMMAND, IDOK, 0);
            return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                self->chosen = self->body.empty() ? ListView_GetNextItem(self->list, -1, LVNI_SELECTED) : -1;
                if (self->body.empty() && self->chosen < 0) return 0;
                self->done = true;
            } else if (LOWORD(wp) == IDCANCEL) self->done = true;
            return 0;
        case WM_CLOSE: self->done = true; return 0;
        case WM_DESTROY: if (self->face) DeleteObject(self->face); return 0;
        }
        return DefWindowProcW(w, message, wp, lp);
    }
    int show(HWND owner, const wchar_t* title) {
        dpi = GetDpiForWindow(owner);
        RECT parent; GetWindowRect(owner, &parent);
        HWND w = CreateWindowExW(WS_EX_DLGMODALFRAME, L"AudioSplitterModal", title, WS_OVERLAPPEDWINDOW,
            parent.left + d(20), parent.top + d(50), d(780), d(470), owner, nullptr, appInstance, this);
        if (!w) throw std::runtime_error("Dialog konnte nicht erstellt werden.");
        EnableWindow(owner, FALSE); ShowWindow(w, SW_SHOW); SetFocus(list);
        MSG message{};
        while (!done && GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(w, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
        }
        EnableWindow(owner, TRUE); DestroyWindow(w); SetForegroundWindow(owner); return chosen;
    }
};

struct Row { std::string id; HWND toggle, menu, slider; };
class Window {
    HWND window_ = nullptr;
    UINT dpi_ = 96;
    int scroll_ = 0, contentHeight_ = 0, width_ = 640;
    bool expanded_ = false, initialized_ = false, preview_ = false;
    std::array<HFONT, 4> fonts_{};
    HFONT iconFont_ = nullptr;
    HBRUSH bgBrush_ = CreateSolidBrush(background), whiteBrush_ = CreateSolidBrush(RGB(255, 255, 255));
    std::map<int, HWND> buttons_;
    std::vector<Row> rows_;
    std::unique_ptr<AudioWorker> audio_;
    Snapshot snapshot_;
    std::atomic<bool> busy_{false};
    std::thread setupThread_;
    std::mutex setupMutex_;
    std::wstring setupMessage_;
    int d(int value) const { return MulDiv(value, dpi_, 96); }
    RECT rect(int x, int y, int w, int h) const { return {d(x), d(y) - scroll_, d(x + w), d(y + h) - scroll_}; }
    void place(HWND control, int x, int y, int w, int h, bool visible = true) {
        auto r = rect(x, y, w, h); MoveWindow(control, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
        ShowWindow(control, visible ? SW_SHOWNA : SW_HIDE);
    }
    void fonts() {
        for (auto f : fonts_) if (f) DeleteObject(f);
        if (iconFont_) DeleteObject(iconFont_);
        fonts_ = {font(14, FW_NORMAL, dpi_), font(12, FW_NORMAL, dpi_), font(18, FW_SEMIBOLD, dpi_), font(30, FW_SEMIBOLD, dpi_)};
        iconFont_ = CreateFontW(-d(24), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe MDL2 Assets");
        for (auto& [id, control] : buttons_) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(fonts_[0]), TRUE);
        for (auto& r : rows_) for (auto control : {r.toggle, r.menu, r.slider}) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(fonts_[0]), TRUE);
    }
    int rowsBottom() const { return 288 + (rows_.empty() ? 75 : static_cast<int>(rows_.size()) * 158); }
    int setupTop() const { return rowsBottom() + 56 + (snapshot_.error.empty() ? 0 : 64); }
    void layout() {
        RECT client; GetClientRect(window_, &client); width_ = MulDiv(client.right, 96, dpi_);
        int right = width_ - 24;
        contentHeight_ = d(setupTop() + (expanded_ ? 276 : 60) + 65);
        SCROLLINFO info{}; info.cbSize = sizeof(info); info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        info.nMin = 0; info.nMax = contentHeight_ - 1; info.nPage = static_cast<UINT>(client.bottom);
        scroll_ = std::clamp(scroll_, 0, std::max(0, contentHeight_ - static_cast<int>(client.bottom))); info.nPos = scroll_;
        SetScrollInfo(window_, SB_VERT, &info, TRUE);
        place(buttons_[addId], right - 142, 238, 142, 36);
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            auto& r = rows_[i]; int y = 288 + static_cast<int>(i) * 158;
            place(r.toggle, right - 156, y + 18, 100, 34); place(r.menu, right - 48, y + 18, 32, 34);
            place(r.slider, 76, y + 105, width_ - 174, 30);
        }
        int y = setupTop();
        place(buttons_[setupId], 40, y + 12, width_ - 80, 34);
        place(buttons_[driverId], 40, y + 87, 172, 34, expanded_);
        place(buttons_[bluetoothId], 222, y + 87, right - 238, 34, expanded_);
        place(buttons_[renameId], 40, y + 130, 172, 34, expanded_);
        place(buttons_[soundId], 222, y + 130, 155, 34, expanded_);
        place(buttons_[sourceId], 387, y + 130, right - 403, 34, expanded_);
        place(buttons_[vendorId], 40, y + 236, width_ - 80, 28, expanded_);
        place(buttons_[aboutId], 65, y + (expanded_ ? 290 : 72), width_ - 130, 30);
        InvalidateRect(window_, nullptr, FALSE);
    }
    void update() {
        if (audio_) snapshot_ = audio_->snapshot();
        if (!initialized_ && (!snapshot_.devices.empty() || !snapshot_.error.empty())) { expanded_ = !snapshot_.sourceReady; initialized_ = true; }
        bool changed = rows_.size() != snapshot_.routes.size();
        if (!changed) for (std::size_t i = 0; i < rows_.size(); ++i) if (rows_[i].id != snapshot_.routes[i].preference.id) changed = true;
        if (changed) {
            for (auto& r : rows_) { DestroyWindow(r.toggle); DestroyWindow(r.menu); DestroyWindow(r.slider); } rows_.clear();
            for (std::size_t i = 0; i < snapshot_.routes.size(); ++i) {
                int base = 100 + static_cast<int>(i) * 3;
                Row r{snapshot_.routes[i].preference.id, button(window_, base, L"Pausieren"), button(window_, base + 1, L"⋯"), nullptr};
                r.slider = CreateWindowExW(0, TRACKBAR_CLASSW, L"Lautstärke", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_NOTICKS, 0, 0, 1, 1, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(base + 2)), appInstance, nullptr);
                SendMessageW(r.slider, TBM_SETRANGE, FALSE, MAKELPARAM(0, 100)); SendMessageW(r.slider, TBM_SETPAGESIZE, 0, 5);
                SetWindowSubclass(r.slider, focusProc, 1, 0);
                rows_.push_back(r);
            }
            HWND previous = buttons_[addId];
            for (const auto& row : rows_) for (HWND control : {row.toggle, row.menu, row.slider}) {
                SetWindowPos(control, previous, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); previous = control;
            }
            for (int id : {setupId, driverId, bluetoothId, renameId, soundId, sourceId, vendorId, aboutId}) {
                SetWindowPos(buttons_[id], previous, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); previous = buttons_[id];
            }
            fonts();
        }
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            const auto& preference = snapshot_.routes[i].preference;
            setText(rows_[i].toggle, preference.enabled ? L"Pausieren" : L"Verbinden");
            // Do not overwrite a slider while it owns focus/capture; the worker may be one snapshot behind.
            if (GetFocus() != rows_[i].slider && GetCapture() != rows_[i].slider)
                SendMessageW(rows_[i].slider, TBM_SETPOS, TRUE, preference.volume);
            SetWindowTextW(rows_[i].slider, (L"Lautstärke " + wide(preference.name)).c_str());
        }
        EnableWindow(buttons_[addId], rows_.size() < maxOutputs && !busy_);
        for (int id : {driverId, renameId, sourceId}) EnableWindow(buttons_[id], !busy_ && (id == driverId || snapshot_.sourceReady || id == sourceId));
        bool installed = std::any_of(snapshot_.devices.begin(), snapshot_.devices.end(), [](const auto& d) { return d.cable; });
        setText(buttons_[driverId], installed ? L"Treiber entfernen …" : L"Treiber installieren …");
        setText(buttons_[setupId], expanded_ ? L"⌄  Einrichtung" : L"›  Einrichtung");
        layout();
    }
    void paint(HDC target) {
        RECT client; GetClientRect(window_, &client);
        HDC dc = CreateCompatibleDC(target); HBITMAP bitmap = CreateCompatibleBitmap(target, std::max(1L, client.right), std::max(1L, client.bottom));
        auto old = SelectObject(dc, bitmap); FillRect(dc, &client, bgBrush_);
        int w = width_ - 48;
        text(dc, fonts_[3], L"Gemeinsam hören", rect(24, 24, w, 42));
        rounded(dc, rect(24, 86, w, 134), RGB(255, 255, 255), static_cast<float>(d(10)));
        text(dc, fonts_[1], L"AUDIOEINGANG", rect(42, 101, w - 36, 20), muted);
        text(dc, fonts_[2], L"Audio Splitter", rect(42, 129, 170, 28));
        text(dc, fonts_[0], snapshot_.sourceReady ? L"● Bereit" : L"● Einrichtung nötig", rect(220, 133, w - 200, 24), snapshot_.sourceReady ? RGB(24, 132, 96) : muted);
        text(dc, fonts_[1], wide(snapshot_.sourceName), rect(42, 164, w - 36, 20), muted);
        text(dc, fonts_[1], L"Im Player oder in Windows als Ausgabe wählen.", rect(42, 188, w - 36, 20), muted);
        text(dc, fonts_[2], L"Kopfhörer · " + std::to_wstring(rows_.size()) + L" von 5", rect(24, 242, w - 150, 30));
        if (rows_.empty()) text(dc, fonts_[0], L"Füge deine in Windows gekoppelten Kopfhörer hinzu.\nAuch USB- und andere Audioausgaben sind möglich.", rect(26, 297, w - 4, 65), muted, DT_WORDBREAK | DT_NOPREFIX);
        for (std::size_t i = 0; i < snapshot_.routes.size(); ++i) {
            const auto& r = snapshot_.routes[i]; int y = 288 + static_cast<int>(i) * 158;
            rounded(dc, rect(24, y, w, 146), RGB(255, 255, 255), static_cast<float>(d(10)));
            rounded(dc, rect(40, y + 18, 36, 44), background, static_cast<float>(d(6)));
            text(dc, iconFont_, L"\xE7F6", rect(46, y + 26, 30, 32));
            text(dc, fonts_[2], wide(r.preference.name), rect(88, y + 17, width_ - 278, 28));
            text(dc, fonts_[1], status(r), rect(88, y + 49, width_ - 132, 22), r.state == RouteState::Playing ? RGB(24, 132, 96) : muted);
            std::wstring detail;
            if (!r.error.empty()) detail = wide(r.error);
            else detail = r.preference.enabled ? L"Automatische Wiederverbindung ist aktiv." : L"Verbindung bleibt pausiert, auch nach einem Neustart.";
            text(dc, fonts_[1], detail, rect(40, y + 78, w - 32, 20), muted);
            text(dc, iconFont_, L"\xE767", rect(40, y + 109, 30, 30), muted);
            int volume = static_cast<int>(SendMessageW(rows_[i].slider, TBM_GETPOS, 0, 0));
            text(dc, fonts_[0], std::to_wstring(volume) + L" %", rect(width_ - 88, y + 111, 48, 22), muted, DT_RIGHT | DT_SINGLELINE);
        }
        text(dc, fonts_[1], L"Lautstärke und Verbindungsstatus werden gespeichert.", rect(26, rowsBottom() + 8, w - 4, 35), muted, DT_WORDBREAK);
        if (!snapshot_.error.empty()) {
            rounded(dc, rect(24, rowsBottom() + 49, w, 60), RGB(255, 241, 223), static_cast<float>(d(6)));
            text(dc, fonts_[1], wide(snapshot_.error), rect(36, rowsBottom() + 57, w - 24, 47), RGB(119, 69, 10), DT_WORDBREAK | DT_NOPREFIX);
        }
        int y = setupTop();
        rounded(dc, rect(24, y, w, expanded_ ? 276 : 60), RGB(255, 255, 255), static_cast<float>(d(10)));
        if (expanded_) {
            text(dc, fonts_[0], snapshot_.sourceReady ? L"Virtueller Audiotreiber: bereit" : L"Virtueller Audiotreiber: nicht bereit", rect(40, y + 56, w - 32, 24), muted);
            std::wstring message; { std::lock_guard lock(setupMutex_); message = setupMessage_; }
            if (!message.empty()) text(dc, fonts_[1], message, rect(40, y + 174, w - 32, 39), muted, DT_WORDBREAK | DT_NOPREFIX);
            text(dc, fonts_[1], L"Nur Stereo-Ausgabe · Kein Mikrofonzugriff", rect(40, y + 212, w - 32, 21), muted);
        }
        BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc);
    }
    void drawButton(const DRAWITEMSTRUCT& item) {
        bool primary = item.CtlID == addId, disabled = item.itemState & ODS_DISABLED, pushed = item.itemState & ODS_SELECTED;
        bool plain = item.CtlID == setupId || item.CtlID == aboutId || item.CtlID == vendorId;
        FillRect(item.hDC, &item.rcItem, item.CtlID == aboutId || primary ? bgBrush_ : whiteBrush_);
        COLORREF fill = plain ? (item.CtlID == aboutId ? background : RGB(255, 255, 255)) : primary ? (disabled ? RGB(154, 174, 207) : pushed ? RGB(26, 77, 177) : blue) : pushed ? RGB(228, 234, 241) : RGB(248, 250, 252);
        rounded(item.hDC, item.rcItem, fill, static_cast<float>(d(6)), plain || primary ? CLR_INVALID : RGB(215, 222, 231));
        RECT bounds = item.rcItem; InflateRect(&bounds, -d(5), 0);
        text(item.hDC, item.CtlID == setupId ? fonts_[2] : item.CtlID == vendorId || item.CtlID == aboutId ? fonts_[1] : fonts_[0], controlText(item.hwndItem), bounds,
            disabled ? RGB(142, 151, 163) : primary ? RGB(255, 255, 255) : item.CtlID == vendorId ? blue : ink,
            DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX | (item.CtlID == setupId ? DT_LEFT : DT_CENTER));
        if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) { InflateRect(&bounds, -d(2), -d(3)); DrawFocusRect(item.hDC, &bounds); }
    }
    void choose(bool source) {
        Modal modal;
        for (const auto& d : snapshot_.devices) {
            if (source ? d.cable : !d.cable && !d.handsFree && std::none_of(rows_.begin(), rows_.end(), [&](const auto& r) { return r.id == d.id; })) modal.items.push_back(d);
        }
        if (modal.items.empty()) {
            MessageBoxW(window_, source ? L"Kein VB-CABLE-Eingang gefunden. Bitte Treiber installieren und Windows neu starten." : L"Keine weiteren Stereo-Ausgaben gefunden. Kopfhörer zuerst in den Windows-Bluetooth-Einstellungen koppeln.", L"Gerät hinzufügen", MB_OK | MB_ICONINFORMATION); return;
        }
        int selected = modal.show(window_, source ? L"Virtuellen Audioeingang wählen" : L"Audioausgabe hinzufügen");
        if (selected >= 0 && audio_) {
            auto d = modal.items[static_cast<std::size_t>(selected)];
            audio_->command([d, source](Settings& s, bool&) { if (source) s.source = d.id; else s.add({d.id, d.name, 70, true}); });
        }
    }
    void about() {
        auto resource = FindResourceW(nullptr, MAKEINTRESOURCEW(200), RT_RCDATA);
        if (!resource) throw std::runtime_error("Lizenzressource fehlt.");
        auto handle = LoadResource(nullptr, resource); auto* bytes = static_cast<const char*>(LockResource(handle));
        std::string license(bytes, SizeofResource(nullptr, resource));
        Modal modal; modal.body = L"BT Audio Splitter 0.1.0\r\nMarcus Beckerle\r\nhttps://github.com/MarcusBeckerle/bt_audio_splitter\r\n\r\n" + wide(license);
        modal.show(window_, L"Über Audio Splitter · Lizenzen");
    }
    void backgroundTask(bool rename) {
        if (busy_.exchange(true)) return;
        if (setupThread_.joinable()) setupThread_.join();
        if (audio_) audio_->command([](Settings&, bool& suspended) { suspended = true; });
        auto source = snapshot_.settings.source;
        setupThread_ = std::thread([this, rename, source] {
            auto progress = [this](const std::wstring& message) { std::lock_guard lock(setupMutex_); setupMessage_ = message; };
            try {
                if (rename) { progress(L"Windows fragt nach Administratorfreigabe …"); runRenameHelper(window_, source); progress(L"Windows-Name wurde auf Audio Splitter gesetzt."); }
                else runDriverSetup(progress);
            } catch (const std::exception& e) { logError(e.what()); progress(wide(e.what())); }
            if (audio_) audio_->command([](Settings&, bool& suspended) { suspended = false; });
            busy_ = false; PostMessageW(window_, setupDone, 0, 0);
        });
    }
    void action(int id) {
        if (id == addId) choose(false);
        else if (id == setupId) { expanded_ = !expanded_; update(); }
        else if (id == bluetoothId) openLink(window_, L"ms-settings:bluetooth");
        else if (id == soundId) openLink(window_, L"ms-settings:sound");
        else if (id == sourceId) choose(true);
        else if (id == vendorId) openLink(window_, L"https://vb-audio.com/Cable/");
        else if (id == aboutId) about();
        else if (id == renameId) backgroundTask(true);
        else if (id == driverId) {
            bool installed = std::any_of(snapshot_.devices.begin(), snapshot_.devices.end(), [](const auto& d) { return d.cable; });
            if (installed && MessageBoxW(window_, L"Das VB-CABLE-Setup wird geöffnet. Dort „Remove Driver“ wählen. Der Treiber kann auch von anderen Programmen verwendet werden. Danach Windows neu starten.", L"Treiber entfernen", MB_OKCANCEL | MB_ICONINFORMATION) != IDOK) return;
            backgroundTask(false);
        } else if (id >= 100 && id < 115) {
            std::size_t index = static_cast<std::size_t>((id - 100) / 3);
            if (index >= rows_.size() || !audio_) return;
            auto endpointId = rows_[index].id;
            if ((id - 100) % 3 == 0) {
                audio_->command([endpointId](Settings& s, bool&) { for (auto& o : s.outputs) if (o.id == endpointId) o.enabled = !o.enabled; });
            } else if ((id - 100) % 3 == 1) {
                HMENU menu = CreatePopupMenu(); AppendMenuW(menu, MF_STRING, 1, L"Geräteinformationen"); AppendMenuW(menu, MF_STRING, 2, L"Gerät entfernen");
                RECT bounds; GetWindowRect(rows_[index].menu, &bounds);
                int chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTALIGN, bounds.right, bounds.bottom, 0, window_, nullptr); DestroyMenu(menu);
                if (chosen == 2) audio_->command([endpointId](Settings& s, bool&) { s.remove(endpointId); });
                else if (chosen == 1) {
                    auto& route = snapshot_.routes[index];
                    auto message = wide(route.preference.name) + L"\n\nWindows-Gerätekennung:\n" + wide(endpointId) + L"\n\n" + status(route);
                    if (!route.error.empty()) message += L"\n" + wide(route.error);
                    MessageBoxW(window_, message.c_str(), L"Verknüpftes Windows-Gerät", MB_OK | MB_ICONINFORMATION);
                }
            }
        }
    }
    void scroll(int position) { scroll_ = position; layout(); }
    LRESULT message(UINT message, WPARAM wp, LPARAM lp) {
        switch (message) {
        case WM_CREATE:
            dpi_ = GetDpiForWindow(window_);
            buttons_[addId] = button(window_, addId, L"＋ Hinzufügen"); buttons_[setupId] = button(window_, setupId, L"› Einrichtung");
            buttons_[driverId] = button(window_, driverId, L"Treiber installieren …"); buttons_[bluetoothId] = button(window_, bluetoothId, L"Bluetooth-Einstellungen");
            buttons_[renameId] = button(window_, renameId, L"Windows-Namen setzen"); buttons_[soundId] = button(window_, soundId, L"Soundeinstellungen");
            buttons_[sourceId] = button(window_, sourceId, L"Eingang wählen"); buttons_[aboutId] = button(window_, aboutId, L"Über Audio Splitter · Lizenzen");
            buttons_[vendorId] = button(window_, vendorId, L"VB-CABLE von VB-Audio · Donationware");
            if (!preview_) audio_ = std::make_unique<AudioWorker>();
            fonts(); update(); SetTimer(window_, 1, 200, nullptr); return 0;
        case WM_SIZE: layout(); return 0;
        case WM_GETMINMAXINFO: {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lp); limits->ptMinTrackSize = {d(600), d(480)}; return 0;
        }
        case WM_DPICHANGED: {
            dpi_ = HIWORD(wp); fonts(); scroll_ = 0;
            auto* r = reinterpret_cast<RECT*>(lp); SetWindowPos(window_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE); layout(); return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(window_, &ps); paint(dc); EndPaint(window_, &ps); return 0; }
        case WM_PRINTCLIENT: paint(reinterpret_cast<HDC>(wp)); return 0;
        case WM_DRAWITEM: drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lp)); return TRUE;
        case WM_NOTIFY: {
            auto* header = reinterpret_cast<NMHDR*>(lp);
            if (header->code == NM_CUSTOMDRAW && std::any_of(rows_.begin(), rows_.end(), [&](const auto& r) { return r.slider == header->hwndFrom; })) {
                auto* draw = reinterpret_cast<NMCUSTOMDRAW*>(lp);
                if (draw->dwDrawStage != CDDS_PREPAINT) return CDRF_DODEFAULT;
                RECT bounds, thumb; GetClientRect(header->hwndFrom, &bounds);
                SendMessageW(header->hwndFrom, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumb));
                FillRect(draw->hdc, &bounds, whiteBrush_);
                int center = (thumb.left + thumb.right) / 2, y = (bounds.top + bounds.bottom) / 2;
                int halfThumb = (thumb.right - thumb.left) / 2;
                RECT channel{bounds.left + halfThumb, y - d(2), bounds.right - halfThumb, y + d(2)};
                rounded(draw->hdc, channel, RGB(224, 230, 238), static_cast<float>(d(2)));
                RECT fill = channel; fill.right = center;
                if (fill.right > fill.left + d(4)) rounded(draw->hdc, fill, blue, static_cast<float>(d(2)));
                RECT dot{center - d(7), y - d(7), center + d(7), y + d(7)};
                rounded(draw->hdc, dot, blue, static_cast<float>(d(7)));
                if (GetFocus() == header->hwndFrom) { InflateRect(&bounds, -1, -1); DrawFocusRect(draw->hdc, &bounds); }
                return CDRF_SKIPDEFAULT;
            }
            return 0;
        }
        case WM_CTLCOLORSTATIC: SetBkColor(reinterpret_cast<HDC>(wp), RGB(255, 255, 255)); return reinterpret_cast<LRESULT>(whiteBrush_);
        case WM_TIMER: update(); return 0;
        case setupDone: update(); return 0;
        case controlFocused: {
            RECT bounds, client; GetWindowRect(reinterpret_cast<HWND>(wp), &bounds); GetClientRect(window_, &client);
            MapWindowPoints(nullptr, window_, reinterpret_cast<POINT*>(&bounds), 2);
            if (bounds.top < 0) scroll(scroll_ + bounds.top - d(12));
            else if (bounds.bottom > client.bottom) scroll(scroll_ + bounds.bottom - client.bottom + d(12));
            return 0;
        }
        case WM_COMMAND: if (HIWORD(wp) == BN_CLICKED) action(LOWORD(wp)); return 0;
        case WM_HSCROLL: {
            HWND slider = reinterpret_cast<HWND>(lp);
            for (const auto& row : rows_) if (row.slider == slider && audio_) {
                auto id = row.id; int volume = static_cast<int>(SendMessageW(slider, TBM_GETPOS, 0, 0));
                audio_->command([id, volume](Settings& s, bool&) { for (auto& o : s.outputs) if (o.id == id) o.volume = volume; });
            }
            InvalidateRect(window_, nullptr, FALSE); return 0;
        }
        case WM_VSCROLL: {
            SCROLLINFO info{}; info.cbSize = sizeof(info); info.fMask = SIF_ALL; GetScrollInfo(window_, SB_VERT, &info); int p = scroll_;
            switch (LOWORD(wp)) {
            case SB_LINEUP: p -= d(32); break; case SB_LINEDOWN: p += d(32); break;
            case SB_PAGEUP: p -= static_cast<int>(info.nPage); break; case SB_PAGEDOWN: p += static_cast<int>(info.nPage); break;
            case SB_THUMBTRACK: p = info.nTrackPos; break; case SB_TOP: p = 0; break; case SB_BOTTOM: p = contentHeight_; break;
            }
            scroll(p); return 0;
        }
        case WM_MOUSEWHEEL: scroll(scroll_ - MulDiv(GET_WHEEL_DELTA_WPARAM(wp), d(60), WHEEL_DELTA)); return 0;
        case WM_CLOSE:
            if (busy_) { MessageBoxW(window_, L"Bitte zuerst das Treiber-Setup bzw. die Administratorabfrage abschließen.", L"Einrichtung läuft", MB_OK | MB_ICONINFORMATION); return 0; }
            DestroyWindow(window_); return 0;
        case WM_DESTROY: KillTimer(window_, 1); if (!preview_) PostQuitMessage(0); return 0;
        }
        return DefWindowProcW(window_, message, wp, lp);
    }
public:
    explicit Window(bool preview) : preview_(preview) {
        if (preview) {
            initialized_ = true; expanded_ = true; snapshot_.sourceReady = true; snapshot_.sourceName = "Audio Splitter (VB-Audio Virtual Cable)";
            snapshot_.devices.push_back({"cable", "Audio Splitter", true, true, false});
            snapshot_.routes = {{{"demo-1", "Sony WH-1000XM5", 75, true}, RouteState::Playing, {}}, {{"demo-2", "Bose QuietComfort", 60, true}, RouteState::Playing, {}}};
        }
    }
    ~Window() {
        if (setupThread_.joinable()) setupThread_.join(); audio_.reset();
        for (auto f : fonts_) if (f) DeleteObject(f); if (iconFont_) DeleteObject(iconFont_);
        DeleteObject(bgBrush_); DeleteObject(whiteBrush_);
    }
    static LRESULT CALLBACK proc(HWND w, UINT message, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(w, GWLP_USERDATA));
        if (message == WM_NCCREATE) { self = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams); self->window_ = w; SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
        if (!self) return DefWindowProcW(w, message, wp, lp);
        try { return self->message(message, wp, lp); }
        catch (const std::exception& e) { logError(e.what()); MessageBoxW(w, wide(e.what()).c_str(), L"Audio Splitter", MB_OK | MB_ICONERROR); return 0; }
    }
    HWND create() {
        UINT dpi = GetDpiForSystem(); dpi_ = dpi;
        RECT area{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
        int height = std::min(d(preview_ ? 1080 : 850), static_cast<int>(area.bottom - area.top - 40));
        int width = d(660);
        HWND w = CreateWindowExW(WS_EX_CONTROLPARENT, L"BtAudioSplitterNative", L"BT Audio Splitter", WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_CLIPCHILDREN,
            area.left + (area.right - area.left - width) / 2, area.top + 20, width, height, nullptr, nullptr, appInstance, this);
        if (!w) throw std::runtime_error("Fenster konnte nicht erstellt werden.");
        return w;
    }
    void preview(const std::filesystem::path& path) {
        // Render our own window off-screen, including native controls. No desktop pixels are captured.
        SetWindowPos(window_, nullptr, 0, 0, d(660), d(1120), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        layout();
        RECT r; GetClientRect(window_, &r); HDC screen = GetDC(window_), memory = CreateCompatibleDC(screen);
        HBITMAP bitmap = CreateCompatibleBitmap(screen, r.right, r.bottom); auto old = SelectObject(memory, bitmap);
        paint(memory);
        for (HWND child = GetWindow(window_, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            if (!(GetWindowLongW(child, GWL_STYLE) & WS_VISIBLE)) continue;
            RECT bounds; GetWindowRect(child, &bounds); MapWindowPoints(nullptr, window_, reinterpret_cast<POINT*>(&bounds), 2);
            int saved = SaveDC(memory); SetViewportOrgEx(memory, bounds.left, bounds.top, nullptr);
            IntersectClipRect(memory, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top);
            SendMessageW(child, WM_PRINT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
            RestoreDC(memory, saved);
        }
        Gdiplus::Bitmap image(bitmap, nullptr);
        CLSID png{0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
        auto result = image.Save(path.c_str(), &png, nullptr);
        SelectObject(memory, old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(window_, screen);
        if (result != Gdiplus::Ok) throw std::runtime_error("Vorschau konnte nicht gespeichert werden.");
        DestroyWindow(window_);
    }
};
} // namespace
int runUi(HINSTANCE instance, const std::filesystem::path& preview) {
    appInstance = instance;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES}; InitCommonControlsEx(&controls);
    Gdiplus::GdiplusStartupInput startup; ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &startup, nullptr) != Gdiplus::Ok) throw std::runtime_error("Windows-Grafik konnte nicht gestartet werden.");
    struct Graphics { ULONG_PTR token; ~Graphics() { Gdiplus::GdiplusShutdown(token); } } graphics{token};
    WNDCLASSEXW cls{}; cls.cbSize = sizeof(cls); cls.hInstance = instance; cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
    cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1)); cls.hIconSm = cls.hIcon; cls.lpfnWndProc = Window::proc; cls.lpszClassName = L"BtAudioSplitterNative";
    RegisterClassExW(&cls); cls.lpfnWndProc = Modal::proc; cls.lpszClassName = L"AudioSplitterModal"; cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1); RegisterClassExW(&cls);
    Window window(!preview.empty()); HWND handle = window.create();
    if (!preview.empty()) { window.preview(preview); return 0; }
    ShowWindow(handle, SW_SHOWNORMAL); UpdateWindow(handle);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(handle, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    return static_cast<int>(message.wParam);
}
} // namespace splitter
