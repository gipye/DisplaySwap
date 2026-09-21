#include "DisplayManager.h"
#include "MonitorWindow.h"
#include "NamedPipeServer.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace
{
    std::wstring Trim(std::wstring s)
    {
        const wchar_t* ws = L" \t\r\n";
        const auto first = s.find_first_not_of(ws);
        if (first == std::wstring::npos)
            return L"";

        const auto last = s.find_last_not_of(ws);
        return s.substr(first, last - first + 1);
    }

    std::wstring ReadVddMatch()
    {
        std::wifstream in(L"config.ini");
        if (!in)
            return L"VDD";

        std::wstring line;
        bool inVdd = false;

        while (std::getline(in, line))
        {
            line = Trim(line);

            if (line.empty() || line[0] == L';' || line[0] == L'#')
                continue;

            if (line.front() == L'[' && line.back() == L']')
            {
                inVdd = (line == L"[VDD]");
                continue;
            }

            if (inVdd)
            {
                const auto pos = line.find(L'=');
                if (pos != std::wstring::npos)
                {
                    const auto key = Trim(line.substr(0, pos));
                    const auto value = Trim(line.substr(pos + 1));

                    if (key == L"Match" && !value.empty())
                        return value;
                }
            }
        }

        return L"VDD";
    }

    std::string Narrow(const std::wstring& s)
    {
        if (s.empty())
            return {};

        const int n = WideCharToMultiByte(
            CP_UTF8,
            0,
            s.data(),
            static_cast<int>(s.size()),
            nullptr,
            0,
            nullptr,
            nullptr);

        if (n <= 0)
            return {};

        std::string out(n, '\0');

        WideCharToMultiByte(
            CP_UTF8,
            0,
            s.data(),
            static_cast<int>(s.size()),
            out.data(),
            n,
            nullptr,
            nullptr);

        return out;
    }

    std::string JsonEscape(const std::wstring& s)
    {
        const std::string input = Narrow(s);
        std::string output;
        output.reserve(input.size() + 2);
        output.push_back('"');

        for (const char c : input)
        {
            switch (c)
            {
            case '\\': output += "\\\\"; break;
            case '"':  output += "\\\""; break;
            case '\r': output += "\\r"; break;
            case '\n': output += "\\n"; break;
            case '\t': output += "\\t"; break;
            default:   output.push_back(c); break;
            }
        }

        output.push_back('"');
        return output;
    }

    const char* ModeName(DesiredMode mode)
    {
        return mode == DesiredMode::VirtualOnly
            ? "virtual"
            : "physical";
    }

    std::string NormalizeCommand(std::string request)
    {
        std::transform(
            request.begin(),
            request.end(),
            request.begin(),
            [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            });

        return request;
    }
}

class Daemon
{
private:
    struct CommandRequest
    {
        DesiredMode mode;
        uint64_t generation;
    };

    struct CommandResult
    {
        bool ok = false;
        std::wstring error;
    };

public:
    Daemon()
        : display_(ReadVddMatch())
    {
    }

    ~Daemon()
    {
        RequestShutdown();
        JoinWorker();

        if (pipe_)
        {
            pipe_->Stop();
            pipe_.reset();
        }
    }

    int Run()
    {
        std::wstring error;

        if (!window_.Create(
                [this]
                {
                    NotifyTopologyChanged();
                },
                [this]
                {
                    NotifyShutdown();
                },
                error))
        {
            std::wcerr << L"[fatal] " << error << L"\n";
            return 1;
        }

        try
        {
            worker_ = std::thread([this]
            {
                WorkerMain();
            });
        }
        catch (...)
        {
            std::wcerr << L"[fatal] Could not start worker thread.\n";
            return 1;
        }

        // Always recover to physical mode before accepting client commands.
        RequestStartupRecovery();

        pipe_ = std::make_unique<NamedPipeServer>(
            L"MonitorSelectorDaemon",
            [this](const std::string& request)
            {
                return HandleCommand(request);
            });

        if (!pipe_->Start(error))
        {
            std::wcerr << L"[fatal] " << error << L"\n";
            RequestShutdown();
            JoinWorker();
            return 1;
        }

        std::wcout
            << L"MonitorSelectorDaemon started. VDD match = ["
            << display_.GetVddMatch()
            << L"]\n";

        window_.Run();

        // Stop accepting new pipe requests first. Existing command callbacks
        // are released by shutdownRequested_ / cvDone_.
        if (pipe_)
        {
            pipe_->Stop();
            pipe_.reset();
        }

        RequestShutdown();
        JoinWorker();

        return 0;
    }

private:
    void RequestStartupRecovery()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            desiredMode_ = DesiredMode::PhysicalOnly;
            startupRecoveryPending_ = true;
            requestGeneration_++;
        }

        cv_.notify_one();
    }

    void NotifyTopologyChanged()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++topologyGeneration_;
            topologyChangePending_ = true;
        }

        cv_.notify_one();
    }

    void NotifyShutdown()
    {
        RequestShutdown();
        window_.Stop();
    }

    void RequestShutdown()
    {
        bool expected = false;
        if (!shutdownRequested_.compare_exchange_strong(expected, true))
            return;

        stopping_ = true;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            shutdownPhysicalPending_ = true;
        }

        cv_.notify_all();
        cvDone_.notify_all();
    }

    std::string HandleCommand(const std::string& rawRequest)
    {
        const std::string request = NormalizeCommand(rawRequest);

        if (request.find("status") != std::string::npos)
            return BuildStatus();

        DesiredMode requested;

        if (request.find("virtual") != std::string::npos)
        {
            requested = DesiredMode::VirtualOnly;
        }
        else if (request.find("physical") != std::string::npos)
        {
            requested = DesiredMode::PhysicalOnly;
        }
        else
        {
            return "{\"ok\":false,\"error\":\"unknown command\"}\n";
        }

        if (shutdownRequested_)
        {
            return "{\"ok\":false,\"error\":\"daemon stopping\"}\n";
        }

        uint64_t generation;

        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (shutdownRequested_)
            {
                return "{\"ok\":false,\"error\":\"daemon stopping\"}\n";
            }

            desiredMode_ = requested;
            generation = ++requestGeneration_;

            // Explicit commands are FIFO. Never collapse these requests.
            commandQueue_.push_back({ requested, generation });
        }

        cv_.notify_one();

        std::unique_lock<std::mutex> lock(mutex_);

        const bool completed = cvDone_.wait_for(
            lock,
            std::chrono::seconds(20),
            [&]
            {
                return shutdownRequested_ ||
                       commandResults_.find(generation) != commandResults_.end();
            });

        if (!completed)
        {
            return
                "{\"ok\":false,"
                "\"error\":\"display configuration timed out\"}\n";
        }

        if (shutdownRequested_)
        {
            return "{\"ok\":false,\"error\":\"daemon stopping\"}\n";
        }

        const auto resultIt = commandResults_.find(generation);
        if (resultIt == commandResults_.end())
        {
            return
                "{\"ok\":false,"
                "\"error\":\"command result unavailable\"}\n";
        }

        const CommandResult result = resultIt->second;
        commandResults_.erase(resultIt);

        if (!result.ok)
        {
            return
                std::string("{\"ok\":false,\"error\":") +
                JsonEscape(result.error) +
                "}\n";
        }

        return
            std::string("{\"ok\":true,\"requested\":\"") +
            ModeName(requested) +
            "\",\"generation\":" +
            std::to_string(generation) +
            "}\n";
    }

    std::string BuildStatus()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::string response = "{\"ok\":true";

        response += ",\"desired\":\"";
        response += ModeName(desiredMode_);
        response += "\"";

        response += ",\"current\":\"";
        response += ModeName(currentMode_);
        response += "\"";

        response += ",\"busy\":";
        response += workerBusy_ ? "true" : "false";

        response += ",\"queueDepth\":";
        response += std::to_string(commandQueue_.size());

        response += ",\"topologyGeneration\":";
        response += std::to_string(topologyGeneration_);

        response += ",\"requestGeneration\":";
        response += std::to_string(requestGeneration_);

        response += ",\"topologyPending\":";
        response += topologyChangePending_ ? "true" : "false";

        response += ",\"stopping\":";
        response += shutdownRequested_ ? "true" : "false";

        response += "}\n";
        return response;
    }

    void CompleteCommand(
        uint64_t generation,
        bool ok,
        const std::wstring& error)
    {
        commandResults_[generation] = CommandResult{ ok, error };
    }

    void WorkerMain()
    {
        for (;;)
        {
            DesiredMode mode = DesiredMode::PhysicalOnly;
            uint64_t generation = 0;
            uint64_t topologyGenerationAtStart = 0;

            bool doApply = false;
            bool isCommand = false;
            bool isStartupRecovery = false;
            bool isShutdownApply = false;

            {
                std::unique_lock<std::mutex> lock(mutex_);

                cv_.wait(lock, [&]
                {
                    return stopping_ ||
                           startupRecoveryPending_ ||
                           shutdownPhysicalPending_ ||
                           !commandQueue_.empty() ||
                           topologyChangePending_;
                });

                // Shutdown cleanup always has priority.
                if (shutdownPhysicalPending_)
                {
                    shutdownPhysicalPending_ = false;
                    mode = DesiredMode::PhysicalOnly;
                    topologyGenerationAtStart = topologyGeneration_;
                    isShutdownApply = true;
                    doApply = true;
                    workerBusy_ = true;
                }
                else if (startupRecoveryPending_)
                {
                    startupRecoveryPending_ = false;
                    mode = DesiredMode::PhysicalOnly;
                    generation = requestGeneration_;
                    topologyGenerationAtStart = topologyGeneration_;
                    isStartupRecovery = true;
                    doApply = true;
                    workerBusy_ = true;
                }
                else if (!commandQueue_.empty())
                {
                    // Explicit client commands are strict FIFO.
                    const CommandRequest request = commandQueue_.front();
                    commandQueue_.pop_front();

                    mode = request.mode;
                    generation = request.generation;
                    topologyGenerationAtStart = topologyGeneration_;
                    isCommand = true;
                    doApply = true;
                    workerBusy_ = true;
                }
                else if (topologyChangePending_)
                {
                    // Topology events are coalesced and reconcile the latest
                    // desired mode rather than creating an unbounded queue.
                    topologyChangePending_ = false;
                    mode = desiredMode_;
                    topologyGenerationAtStart = topologyGeneration_;
                    doApply = true;
                    workerBusy_ = true;
                }
            }

            if (!doApply)
                continue;

            std::wstring error;

            std::wcout
                << L"[display] applying "
                << (mode == DesiredMode::VirtualOnly
                        ? L"VirtualOnly"
                        : L"PhysicalOnly")
                << L"\n";

            const bool ok = display_.Apply(mode, error);

            {
                std::lock_guard<std::mutex> lock(mutex_);

                workerBusy_ = false;

                if (ok)
                {
                    currentMode_ = mode;

                    if (isCommand)
                        CompleteCommand(generation, true, L"");

                    if (isStartupRecovery)
                    {
                        // Nothing else is required. PhysicalOnly is now the
                        // known recovered baseline.
                    }

                    std::wcout
                        << L"[display] applied "
                        << (mode == DesiredMode::VirtualOnly
                                ? L"VirtualOnly"
                                : L"PhysicalOnly")
                        << L"\n";
                }
                else
                {
                    if (isCommand)
                        CompleteCommand(
                            generation,
                            false,
                            error.empty()
                                ? L"DisplayManager::Apply failed."
                                : error);

                    std::wcerr
                        << L"[display] apply failed: "
                        << (error.empty()
                                ? L"DisplayManager::Apply failed."
                                : error)
                        << L"\n";
                }

                // If topology changed during SetDisplayConfig/Verify, perform
                // one more reconciliation against the latest topology.
                if (topologyGeneration_ != topologyGenerationAtStart)
                    topologyChangePending_ = true;
            }

            cvDone_.notify_all();

            if (isShutdownApply)
            {
                // PhysicalOnly was the final operation. Do not process any
                // queued client/topology work after shutdown has begun.
                return;
            }
        }
    }

    void JoinWorker()
    {
        if (worker_.joinable())
            worker_.join();
    }

private:
    DisplayManager display_;
    MonitorWindow window_;
    std::unique_ptr<NamedPipeServer> pipe_;

    std::thread worker_;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable cvDone_;

    std::atomic<bool> stopping_{false};
    std::atomic<bool> shutdownRequested_{false};

    DesiredMode desiredMode_ = DesiredMode::PhysicalOnly;
    DesiredMode currentMode_ = DesiredMode::PhysicalOnly;

    uint64_t requestGeneration_ = 0;
    uint64_t topologyGeneration_ = 0;

    bool startupRecoveryPending_ = false;
    bool shutdownPhysicalPending_ = false;
    bool topologyChangePending_ = false;
    bool workerBusy_ = false;

    std::deque<CommandRequest> commandQueue_;
    std::map<uint64_t, CommandResult> commandResults_;
};

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    UNREFERENCED_PARAMETER(hInstance);
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(pCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    Daemon daemon;
    return daemon.Run();
}