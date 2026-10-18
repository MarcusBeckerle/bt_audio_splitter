#pragma once
#ifndef UNICODE
#define UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include "core.hpp"

namespace splitter {
inline std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!n) throw std::runtime_error("Ungültiges UTF-8.");
    std::wstring result(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), result.data(), n);
    return result;
}
inline std::string utf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string result(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), result.data(), n, nullptr, nullptr);
    return result;
}
inline void check(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        char code[24]; sprintf_s(code, " (0x%08lX)", static_cast<unsigned long>(hr));
        throw std::runtime_error(std::string(operation) + code);
    }
}
// Minimal COM ownership without a framework dependency. Objects never leave their owner thread.
template<class T> class Com {
    T* p_ = nullptr;
public:
    Com() = default;
    ~Com() { reset(); }
    Com(const Com&) = delete; Com& operator=(const Com&) = delete;
    Com(Com&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    Com& operator=(Com&& o) noexcept { if (this != &o) { reset(); p_ = o.p_; o.p_ = nullptr; } return *this; }
    T** put() { reset(); return &p_; }
    T* get() const { return p_; }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    void reset() { if (p_) { p_->Release(); p_ = nullptr; } }
};
struct CoMemory { void operator()(void* p) const { CoTaskMemFree(p); } };
class ComApartment {
public:
    explicit ComApartment(DWORD model = COINIT_MULTITHREADED) { check(CoInitializeEx(nullptr, model), "COM initialisieren"); }
    ~ComApartment() { CoUninitialize(); }
};
class Handle {
    HANDLE h_ = nullptr;
public:
    explicit Handle(HANDLE h = nullptr) : h_(h) {}
    ~Handle() { if (h_ && h_ != INVALID_HANDLE_VALUE) CloseHandle(h_); }
    Handle(const Handle&) = delete; Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return h_; }
};
std::filesystem::path dataDirectory();
std::string readFile(const std::filesystem::path& path, std::size_t limit = 100000);
void atomicWrite(const std::filesystem::path& path, const std::string& text);
Settings loadSettings(std::string& warning);
void logError(const std::string& message) noexcept;
std::wstring executablePath();
std::wstring quoteArgument(const std::wstring& value);
} // namespace splitter
