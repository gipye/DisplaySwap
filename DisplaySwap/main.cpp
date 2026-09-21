#include <windows.h>

#include <algorithm>
#include <iostream>
#include <string>
#include <cwctype>

namespace
{
    constexpr wchar_t PIPE_NAME[] =
        L"\\\\.\\pipe\\MonitorSelectorDaemon";

    constexpr int CONNECT_RETRIES = 20;
    constexpr DWORD CONNECT_WAIT_MS = 500;

    bool IsValidCommand(const std::wstring& command)
    {
        return command == L"virtual" ||
               command == L"physical" ||
               command == L"status";
    }

    std::wstring ToLower(std::wstring value)
    {
        std::transform(
            value.begin(),
            value.end(),
            value.begin(),
            [](wchar_t c)
            {
                return static_cast<wchar_t>(std::towlower(c));
            });

        return value;
    }

    bool Utf8FromWide(
        const std::wstring& input,
        std::string& output)
    {
        if (input.empty())
        {
            output.clear();
            return true;
        }

        const int size = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            input.data(),
            static_cast<int>(input.size()),
            nullptr,
            0,
            nullptr,
            nullptr);

        if (size <= 0)
            return false;

        output.resize(size);

        const int converted = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            input.data(),
            static_cast<int>(input.size()),
            output.data(),
            size,
            nullptr,
            nullptr);

        return converted == size;
    }

    HANDLE ConnectToDaemon()
    {
        for (int attempt = 0;
             attempt < CONNECT_RETRIES;
             ++attempt)
        {
            HANDLE pipe = CreateFileW(
                PIPE_NAME,
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                OPEN_EXISTING,
                0,
                nullptr);

            if (pipe != INVALID_HANDLE_VALUE)
            {
                DWORD mode = PIPE_READMODE_BYTE;

                SetNamedPipeHandleState(
                    pipe,
                    &mode,
                    nullptr,
                    nullptr);

                return pipe;
            }

            const DWORD error = GetLastError();

            if (error != ERROR_PIPE_BUSY)
            {
                std::wcerr
                    << L"Could not connect to daemon. "
                    << L"Win32 error: "
                    << error
                    << L"\n";

                return INVALID_HANDLE_VALUE;
            }

            if (!WaitNamedPipeW(
                    PIPE_NAME,
                    CONNECT_WAIT_MS))
            {
                if (GetLastError() != ERROR_SEM_TIMEOUT)
                {
                    std::wcerr
                        << L"WaitNamedPipeW failed. "
                        << L"Win32 error: "
                        << GetLastError()
                        << L"\n";

                    return INVALID_HANDLE_VALUE;
                }
            }
        }

        std::wcerr
            << L"Timed out waiting for "
            << L"MonitorSelectorDaemon.\n";

        return INVALID_HANDLE_VALUE;
    }

    bool WriteAll(
        HANDLE pipe,
        const std::string& data)
    {
        size_t offset = 0;

        while (offset < data.size())
        {
            const size_t remaining =
                data.size() - offset;

            const DWORD chunk =
                static_cast<DWORD>(
                    std::min<size_t>(
                        remaining,
                        static_cast<size_t>(
                            MAXDWORD)));

            DWORD written = 0;

            if (!WriteFile(
                    pipe,
                    data.data() + offset,
                    chunk,
                    &written,
                    nullptr))
            {
                return false;
            }

            if (written == 0)
                return false;

            offset += written;
        }

        return true;
    }

    bool ReadResponse(
        HANDLE pipe,
        std::string& response)
    {
        response.clear();

        char buffer[4096];

        for (;;)
        {
            DWORD read = 0;

            if (!ReadFile(
                    pipe,
                    buffer,
                    sizeof(buffer),
                    &read,
                    nullptr))
            {
                const DWORD error = GetLastError();

                /*
                 * The daemon normally closes the pipe after
                 * sending the complete response.
                 */
                if (error == ERROR_BROKEN_PIPE)
                    return !response.empty();

                return false;
            }

            if (read == 0)
                break;

            response.append(
                buffer,
                buffer + read);

            /*
             * The daemon response is expected to be one JSON
             * object terminated by '\n'.
             */
            if (response.find('\n') !=
                std::string::npos)
            {
                break;
            }

            /*
             * Prevent an accidentally broken daemon from
             * causing unbounded memory growth.
             */
            if (response.size() > 1024 * 1024)
                return false;
        }

        return !response.empty();
    }

    bool ResponseIsOk(
        const std::string& response)
    {
        /*
         * The daemon protocol currently returns JSON.
         *
         * Keep this deliberately simple because the client
         * only needs to determine whether the request succeeded.
         */
        return response.find("\"ok\":true") !=
               std::string::npos;
    }
}

int wmain(
    int argc,
    wchar_t** argv)
{
    if (argc < 2)
    {
        std::wcerr
            << L"Usage: "
            << L"MonitorSwitch.exe "
            << L"virtual|physical|status\n";

        return 2;
    }

    std::wstring command =
        ToLower(argv[1]);

    if (!IsValidCommand(command))
    {
        std::wcerr
            << L"Unknown command: "
            << command
            << L"\n"
            << L"Usage: "
            << L"MonitorSwitch.exe "
            << L"virtual|physical|status\n";

        return 2;
    }

    /*
     * Convert the protocol command to UTF-8.
     *
     * Current commands are ASCII, but doing this properly avoids
     * creating a hidden encoding dependency in the protocol.
     */
    std::string request;

    if (!Utf8FromWide(
            command + L"\n",
            request))
    {
        std::wcerr
            << L"Failed to encode request as UTF-8.\n";

        return 3;
    }

    HANDLE pipe =
        ConnectToDaemon();

    if (pipe == INVALID_HANDLE_VALUE)
        return 4;

    /*
     * Send only the desired state/query.
     *
     * MonitorSwitch never touches display topology directly.
     */
    if (!WriteAll(
            pipe,
            request))
    {
        std::wcerr
            << L"WriteFile failed. "
            << L"Win32 error: "
            << GetLastError()
            << L"\n";

        CloseHandle(pipe);
        return 5;
    }

    /*
     * Tell the daemon that no more request data is coming.
     *
     * This is useful if the daemon reads until the client finishes
     * its request.
     */
    if (!FlushFileBuffers(pipe))
    {
        std::wcerr
            << L"FlushFileBuffers failed. "
            << L"Win32 error: "
            << GetLastError()
            << L"\n";

        CloseHandle(pipe);
        return 6;
    }

    std::string response;

    if (!ReadResponse(
            pipe,
            response))
    {
        std::wcerr
            << L"Failed to read daemon response. "
            << L"Win32 error: "
            << GetLastError()
            << L"\n";

        CloseHandle(pipe);
        return 7;
    }

    CloseHandle(pipe);

    /*
     * Preserve the daemon's JSON response for logging/debugging.
     */
    std::cout << response;

    if (ResponseIsOk(response))
        return 0;

    return 1;
}