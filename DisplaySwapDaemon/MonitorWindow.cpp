#include "MonitorWindow.h"

#include <dbt.h>

namespace
{
    constexpr wchar_t kClassName[] =
        L"MonitorSelectorDaemonMessageWindow";
}

MonitorWindow::~MonitorWindow()
{
    Stop();

    if (classAtom_ && instance_)
        UnregisterClassW(kClassName, instance_);
}

bool MonitorWindow::Create(
    DisplayCallback displayCallback,
    ShutdownCallback shutdownCallback,
    std::wstring& error)
{
    displayCallback_ = std::move(displayCallback);
    shutdownCallback_ = std::move(shutdownCallback);
    instance_ = GetModuleHandleW(nullptr);

    WNDCLASSW wc{};
    wc.lpfnWndProc = &MonitorWindow::WndProc;
    wc.hInstance = instance_;
    wc.lpszClassName = kClassName;

    classAtom_ = RegisterClassW(&wc);
    if (!classAtom_ && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        error = L"RegisterClassW failed.";
        return false;
    }

    hwnd_ = CreateWindowExW(
        0,
        kClassName,
        L"MonitorSelectorDaemon",
        0,
        0, 0, 0, 0,
        HWND_MESSAGE,
        nullptr,
        instance_,
        this);

    if (!hwnd_)
    {
        error = L"CreateWindowExW failed.";
        return false;
    }

    return true;
}

LRESULT CALLBACK MonitorWindow::WndProc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam)
{
    auto* self = reinterpret_cast<MonitorWindow*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<MonitorWindow*>(cs->lpCreateParams);

        SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(self));

        self->hwnd_ = hwnd;
    }

    if (self)
    {
        switch (msg)
        {
        case WM_DEVICECHANGE:
        case WM_DISPLAYCHANGE:
        case WM_SETTINGCHANGE:
            if (self->displayCallback_)
                self->displayCallback_();
            return 0;

        case WM_QUERYENDSESSION:
            if (self->shutdownCallback_)
                self->shutdownCallback_();

            // Allow Windows shutdown/logoff to proceed.
            return TRUE;

        case WM_ENDSESSION:
            if (wParam && self->shutdownCallback_)
                self->shutdownCallback_();
            return 0;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void MonitorWindow::Run()
{
    MSG msg{};

    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void MonitorWindow::Stop()
{
    HWND hwnd = hwnd_;

    if (hwnd)
    {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        hwnd_ = nullptr;
    }
}
