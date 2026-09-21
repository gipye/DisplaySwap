#pragma once

#include <windows.h>

#include <atomic>
#include <functional>
#include <string>
#include <thread>

class NamedPipeServer
{
public:
    using CommandCallback = std::function<std::string(const std::string&)>;

    NamedPipeServer(std::wstring pipeName, CommandCallback callback);
    ~NamedPipeServer();

    bool Start(std::wstring& error);
    void Stop();

private:
    void ThreadMain();

    std::wstring pipeName_;
    CommandCallback callback_;
    std::thread thread_;
    std::atomic<bool> stopping_{false};
};
