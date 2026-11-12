#ifdef _WIN32
#include "Config.h"
#include "ConnectionManager.h"
#include "Logger.h"
#include "ProxyServer.h"
#include "../resources/resource.h"

#include <windows.h>

#include <atomic>
#include <exception>
#include <memory>
#include <string>
#include <thread>

namespace {
constexpr int kStartId = 1001;
constexpr int kStopId = 1002;
constexpr int kStatusId = 1003;
constexpr int kStatsId = 1004;
constexpr int kEndpointId = 1005;

struct Runtime {
    std::unique_ptr<Logger> logger;
    std::unique_ptr<ConnectionManager> connections;
    std::unique_ptr<ProxyServer> server;
    std::thread worker;
    std::atomic_bool shutdown{false};
    std::atomic_bool running{false};
    std::atomic_bool remote_tls_mode{true};
    int exit_code = 0;
};
Runtime g_runtime;
HWND g_status = nullptr;
HWND g_stats = nullptr;
HWND g_endpoint = nullptr;
HWND g_start = nullptr;
HWND g_stop = nullptr;

void SetStatus(const std::wstring& text) {
    if (g_status) SetWindowTextW(g_status, text.c_str());
}
void StopProxy() {
    if (g_runtime.server) {
        g_runtime.shutdown.store(true, std::memory_order_relaxed);
        g_runtime.server->RequestStop();
    }
    if (g_runtime.worker.joinable()) g_runtime.worker.join();
    g_runtime.running.store(false, std::memory_order_relaxed);
    g_runtime.server.reset();
    g_runtime.connections.reset();
    g_runtime.logger.reset();
    if (g_start) EnableWindow(g_start, TRUE);
    if (g_stop) EnableWindow(g_stop, FALSE);
    SetStatus(L"Stopped");
}

void StartProxy() {
    if (g_runtime.running.load(std::memory_order_relaxed)) return;
    if (g_runtime.worker.joinable()) g_runtime.worker.join();
    // A previous run may have failed during bind/startup. Destroy the server
    // before the Logger and ConnectionManager objects it references.
    g_runtime.server.reset();
    g_runtime.connections.reset();
    g_runtime.logger.reset();
    bool loaded = false;
    AppConfig config;
    try {
        config = AppConfig::LoadFromFile("config/client.json", loaded);
    } catch (const std::exception& ex) {
        SetStatus(L"Configuration error. Check logs/wa-proxy.log.");
        MessageBoxA(nullptr, ex.what(), "WA Proxy configuration error", MB_OK | MB_ICONERROR);
        return;
    }
    if (!loaded) {
        SetStatus(L"Missing config/client.json; copy and edit config/client.example.json first.");
        MessageBoxW(nullptr,
            L"Create config\\client.json from config\\client.example.json and configure your VPS address and certificate paths. The GUI will not silently start direct mode.",
            L"WA Proxy configuration required", MB_OK | MB_ICONWARNING);
        return;
    }
    g_runtime.logger = std::make_unique<Logger>(config.log_file);
    std::string log_error;
    if (!g_runtime.logger->Initialize(log_error)) {
        MessageBoxA(nullptr, log_error.c_str(), "WA Proxy log error", MB_OK | MB_ICONWARNING);
    }
    g_runtime.connections = std::make_unique<ConnectionManager>();
    g_runtime.server = std::make_unique<ProxyServer>(config, *g_runtime.logger, *g_runtime.connections);
    g_runtime.shutdown.store(false, std::memory_order_relaxed);
    g_runtime.remote_tls_mode.store(config.remote_tls_enabled, std::memory_order_relaxed);
    g_runtime.running.store(true, std::memory_order_relaxed);
    g_runtime.worker = std::thread([] {
        try {
            g_runtime.exit_code = g_runtime.server->Run(g_runtime.shutdown);
        } catch (const std::exception& ex) {
            if (g_runtime.logger) g_runtime.logger->Error(std::string("Proxy worker failed: ") + ex.what());
            g_runtime.exit_code = 1;
        } catch (...) {
            if (g_runtime.logger) g_runtime.logger->Error("Proxy worker failed with an unknown exception.");
            g_runtime.exit_code = 1;
        }
        g_runtime.running.store(false, std::memory_order_relaxed);
    });
    EnableWindow(g_start, FALSE);
    EnableWindow(g_stop, TRUE);
    SetStatus(config.remote_tls_enabled ? L"Starting — authenticated TLS/VPS mode" : L"Starting — direct mode (ISP can see destinations)");
}

void UpdateStats() {
    if (!g_runtime.running.load(std::memory_order_relaxed) || !g_runtime.connections) {
        if (g_runtime.server && !g_runtime.running.load(std::memory_order_relaxed)) {
            SetStatus(L"Stopped — see logs/wa-proxy.log for details");
            EnableWindow(g_start, TRUE);
            EnableWindow(g_stop, FALSE);
        }
        return;
    }
    const auto stats = g_runtime.connections->Snapshot();
    const std::wstring text = L"Accepted: " + std::to_wstring(stats.accepted_connections) +
        L"    Active: " + std::to_wstring(stats.active_connections) +
        L"    Rejected: " + std::to_wstring(stats.rejected_connections) +
        L"\r\nBytes received: " + std::to_wstring(stats.bytes_received) +
        L"    Bytes sent: " + std::to_wstring(stats.bytes_sent);
    SetWindowTextW(g_stats, text.c_str());
    SetStatus(g_runtime.remote_tls_mode.load(std::memory_order_relaxed)
        ? L"Running — authenticated TLS/VPS mode"
        : L"Running — direct mode (ISP may see destinations)");
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE:
        CreateWindowW(L"STATIC", L"WA Proxy 1.0", WS_CHILD | WS_VISIBLE,
                      22, 18, 360, 32, hwnd, nullptr, nullptr, nullptr);
        g_status = CreateWindowW(L"STATIC", L"Stopped", WS_CHILD | WS_VISIBLE,
                                  22, 64, 520, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStatusId)), nullptr, nullptr);
        g_endpoint = CreateWindowW(L"STATIC", L"Local proxy: 127.0.0.1:8080  |  Configure: config\\client.json",
                                   WS_CHILD | WS_VISIBLE, 22, 96, 580, 28, hwnd,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEndpointId)), nullptr, nullptr);
        g_stats = CreateWindowW(L"STATIC", L"Accepted: 0    Active: 0    Rejected: 0\r\nBytes received: 0    Bytes sent: 0",
                                WS_CHILD | WS_VISIBLE, 22, 132, 590, 52, hwnd,
                                reinterpret_cast<HMENU>(kStatsId), nullptr, nullptr);
        g_start = CreateWindowW(L"BUTTON", L"Start Proxy", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                22, 205, 130, 38, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStartId)), nullptr, nullptr);
        g_stop = CreateWindowW(L"BUTTON", L"Stop Proxy", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED,
                               170, 205, 130, 38, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStopId)), nullptr, nullptr);
        SetTimer(hwnd, 1, 1000, nullptr);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wparam) == kStartId) { StartProxy(); return 0; }
        if (LOWORD(wparam) == kStopId) { StopProxy(); return 0; }
        break;
    case WM_TIMER:
        UpdateStats();
        return 0;
    case WM_CLOSE:
        StopProxy();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    // Resolve relative config/certificate/log paths from the executable folder,
    // even when the user launches the GUI from a shortcut or another directory.
    std::wstring executable_path(32768, L'\0');
    const DWORD path_length = GetModuleFileNameW(nullptr, executable_path.data(),
                                                  static_cast<DWORD>(executable_path.size()));
    if (path_length > 0 && path_length < executable_path.size()) {
        executable_path.resize(path_length);
        const auto slash = executable_path.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            executable_path.resize(slash);
            SetCurrentDirectoryW(executable_path.c_str());
        }
    }
    const wchar_t* class_name = L"WAProxyMainWindow";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = class_name;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_WAPROXY));
    wc.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(IDI_WAPROXY));
    if (!RegisterClassExW(&wc)) return 1;
    HWND hwnd = CreateWindowExW(0, class_name, L"WA Proxy — Secure Tunnel Client",
                                WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, 650, 310,
                                nullptr, nullptr, instance, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, show_command);
    UpdateWindow(hwnd);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
#else
int main() { return 1; }
#endif
