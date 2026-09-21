#include "NamedPipeServer.h"

#include <sddl.h>

namespace
{
    constexpr DWORD kBufferSize = 64 * 1024;
    constexpr DWORD kPipeInstances = 8;
}

NamedPipeServer::NamedPipeServer(
    std::wstring pipeName,
    CommandCallback callback)
    : pipeName_(std::move(pipeName)),
      callback_(std::move(callback))
{
}

NamedPipeServer::~NamedPipeServer()
{
    Stop();
}

bool NamedPipeServer::Start(std::wstring& error)
{
    if (thread_.joinable())
    {
        error = L"Named pipe server is already running.";
        return false;
    }

    stopping_ = false;

    try
    {
        thread_ = std::thread(&NamedPipeServer::ThreadMain, this);
    }
    catch (...)
    {
        error = L"Could not start named pipe thread.";
        return false;
    }

    return true;
}

void NamedPipeServer::Stop()
{
    if (stopping_.exchange(true))
        return;

    const std::wstring fullName = L"\\\\.\\pipe\\" + pipeName_;

    // Wake a blocking ConnectNamedPipe().
    HANDLE wake = CreateFileW(
        fullName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);

    if (wake != INVALID_HANDLE_VALUE)
        CloseHandle(wake);

    if (thread_.joinable())
        thread_.join();
}

void NamedPipeServer::ThreadMain()
{
    const std::wstring fullName = L"\\\\.\\pipe\\" + pipeName_;

    PSECURITY_DESCRIPTOR sd = nullptr;
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);

    // Interactive Users + SYSTEM.
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;;GA;;;IU)(A;;GA;;;SY)",
            SDDL_REVISION_1,
            &sd,
            nullptr))
    {
        sa.lpSecurityDescriptor = sd;
    }

    while (!stopping_)
    {
        HANDLE pipe = CreateNamedPipeW(
            fullName.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            kPipeInstances,
            kBufferSize,
            kBufferSize,
            1000,
            sd ? &sa : nullptr);

        if (pipe == INVALID_HANDLE_VALUE)
            break;

        const BOOL connected =
            ConnectNamedPipe(pipe, nullptr)
                ? TRUE
                : (GetLastError() == ERROR_PIPE_CONNECTED);

        if (connected && !stopping_)
        {
            std::string request;
            request.reserve(256);

            char buffer[4096];
            bool readOk = true;

            for (;;)
            {
                DWORD bytesRead = 0;

                if (!ReadFile(
                        pipe,
                        buffer,
                        sizeof(buffer),
                        &bytesRead,
                        nullptr))
                {
                    readOk = false;
                    break;
                }

                if (bytesRead == 0)
                    break;

                request.append(buffer, buffer + bytesRead);

                if (request.find('\n') != std::string::npos)
                    break;

                if (request.size() >= kBufferSize)
                {
                    readOk = false;
                    break;
                }
            }

            if (readOk && !stopping_)
            {
                std::string response;

                try
                {
                    response = callback_
                        ? callback_(request)
                        : "{\"ok\":false,\"error\":\"server callback unavailable\"}\n";
                }
                catch (...)
                {
                    response =
                        "{\"ok\":false,\"error\":\"internal server exception\"}\n";
                }

                if (response.empty() || response.back() != '\n')
                    response.push_back('\n');

                const char* data = response.data();
                size_t remaining = response.size();

                while (remaining > 0)
                {
                    DWORD written = 0;
                    const DWORD chunk = static_cast<DWORD>(
                        remaining > MAXDWORD ? MAXDWORD : remaining);

                    if (!WriteFile(pipe, data, chunk, &written, nullptr))
                        break;

                    if (written == 0)
                        break;

                    data += written;
                    remaining -= written;
                }

                FlushFileBuffers(pipe);
            }
        }

        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }

    if (sd)
        LocalFree(sd);
}
