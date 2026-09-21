#pragma once

#include <windows.h>

#include <functional>
#include <string>

class MonitorWindow
{
public:
    using DisplayCallback = std::function<void()>;
    using ShutdownCallback = std::function<void()>;

    MonitorWindow() = default;
    ~MonitorWindow();

    bool Create(
        DisplayCallback displayCallback,
        ShutdownCallback shutdownCallback,
        std::wstring& error);

    void Run();
    void Stop();

private:
    static LRESULT CALLBACK WndProc(
        HWND hwnd,
        UINT msg,
        WPARAM wParam,
        LPARAM lParam);

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    DisplayCallback displayCallback_;
    ShutdownCallback shutdownCallback_;
    ATOM classAtom_ = 0;
};
