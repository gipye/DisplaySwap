#include "DisplayManager.h"

#include <algorithm>
#include <cwctype>
#include <iostream>
#include <sstream>

namespace
{
    bool SameLuid(
        const LUID& a,
        const LUID& b)
    {
        return a.HighPart == b.HighPart &&
               a.LowPart == b.LowPart;
    }

    bool IsVirtualAware(
        const DISPLAYCONFIG_PATH_INFO& path)
    {
        return
            (path.flags &
             DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE) != 0;
    }

    void InvalidatePathModes(
        DISPLAYCONFIG_PATH_INFO& path)
    {
        if (IsVirtualAware(path))
        {
            path.sourceInfo.sourceModeInfoIdx =
                DISPLAYCONFIG_PATH_SOURCE_MODE_IDX_INVALID;

            path.sourceInfo.cloneGroupId =
                DISPLAYCONFIG_PATH_CLONE_GROUP_INVALID;

            path.targetInfo.targetModeInfoIdx =
                DISPLAYCONFIG_PATH_TARGET_MODE_IDX_INVALID;

            path.targetInfo.desktopModeInfoIdx =
                DISPLAYCONFIG_PATH_DESKTOP_IMAGE_IDX_INVALID;
        }
        else
        {
            path.sourceInfo.modeInfoIdx =
                DISPLAYCONFIG_PATH_MODE_IDX_INVALID;

            path.targetInfo.modeInfoIdx =
                DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
        }
    }

    const wchar_t* BoolText(
        bool value)
    {
        return value ? L"true" : L"false";
    }
}

DisplayManager::DisplayManager(
    std::wstring vddMatch)
    : vddMatch_(ToLower(std::move(vddMatch)))
{
}

std::wstring DisplayManager::ToLower(
    std::wstring value)
{
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](wchar_t c)
        {
            return static_cast<wchar_t>(
                std::towlower(c));
        });

    return value;
}

std::wstring DisplayManager::LastErrorMessage(
    LONG error)
{
    if (error == ERROR_SUCCESS)
    {
        return L"";
    }

    wchar_t* buffer = nullptr;

    DWORD length =
        FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            static_cast<DWORD>(error),
            0,
            reinterpret_cast<LPWSTR>(&buffer),
            0,
            nullptr);

    std::wstring message;

    if (length != 0 && buffer != nullptr)
    {
        message.assign(
            buffer,
            buffer + length);

        while (!message.empty() &&
               (message.back() == L'\r' ||
                message.back() == L'\n' ||
                message.back() == L' '))
        {
            message.pop_back();
        }
    }

    if (buffer != nullptr)
    {
        LocalFree(buffer);
    }

    std::wstringstream result;

    result << L"Win32 error "
           << error
           << L": "
           << message;

    return result.str();
}

bool DisplayManager::GetTargetName(
    const DISPLAYCONFIG_PATH_INFO& path,
    std::wstring& friendly,
    std::wstring& devicePath)
{
    friendly.clear();
    devicePath.clear();

    DISPLAYCONFIG_TARGET_DEVICE_NAME targetName{};

    targetName.header.type =
        DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;

    targetName.header.size =
        sizeof(targetName);

    targetName.header.adapterId =
        path.targetInfo.adapterId;

    targetName.header.id =
        path.targetInfo.id;

    LONG result =
        DisplayConfigGetDeviceInfo(
            &targetName.header);

    if (result != ERROR_SUCCESS)
    {
        return false;
    }

    friendly =
        targetName.monitorFriendlyDeviceName;

    devicePath =
        targetName.monitorDevicePath;

    return true;
}

bool DisplayManager::IsVdd(
    const DisplayPathInfo& path) const
{
    std::wstring friendly =
        ToLower(path.friendlyName);

    std::wstring device =
        ToLower(path.devicePath);

    if (!vddMatch_.empty())
    {
        if (friendly.find(vddMatch_) !=
            std::wstring::npos)
        {
            return true;
        }

        if (device.find(vddMatch_) !=
            std::wstring::npos)
        {
            return true;
        }
    }

    return false;
}

bool DisplayManager::BuildTopologyOnce(
    std::vector<DISPLAYCONFIG_PATH_INFO>& paths,
    std::vector<DISPLAYCONFIG_MODE_INFO>& modes,
    std::wstring& error)
{
    paths.clear();
    modes.clear();
    error.clear();

    UINT32 pathCount = 0;
    UINT32 modeCount = 0;

    LONG result =
        GetDisplayConfigBufferSizes(
            QDC_ALL_PATHS | QDC_VIRTUAL_MODE_AWARE,
            &pathCount,
            &modeCount);

    if (result != ERROR_SUCCESS)
    {
        error =
            LastErrorMessage(result);

        return false;
    }

    if (pathCount == 0)
    {
        error =
            L"No display paths were returned.";

        return false;
    }

    paths.resize(pathCount);
    modes.resize(modeCount);

    result =
        QueryDisplayConfig(
            QDC_ALL_PATHS | QDC_VIRTUAL_MODE_AWARE,
            &pathCount,
            paths.data(),
            &modeCount,
            modes.data(),
            nullptr);

    if (result != ERROR_SUCCESS)
    {
        error =
            LastErrorMessage(result);

        paths.clear();
        modes.clear();

        return false;
    }

    paths.resize(pathCount);
    modes.resize(modeCount);

    std::wcout
        << L"[debug] QueryDisplayConfig: paths="
        << paths.size()
        << L", modes="
        << modes.size()
        << std::endl;

    for (size_t i = 0;
         i < paths.size();
         ++i)
    {
        const auto& path =
            paths[i];

        std::wstring friendly;
        std::wstring devicePath;

        GetTargetName(
            path,
            friendly,
            devicePath);

        std::wcout
            << L"[debug] path["
            << i
            << L"] "
            << (friendly.empty()
                    ? L"<unknown>"
                    : friendly)
            << L" flags=0x"
            << std::hex
            << path.flags
            << std::dec
            << L" active="
            << BoolText(
                   (path.flags &
                    DISPLAYCONFIG_PATH_ACTIVE) != 0)
            << L" virtualAware="
            << BoolText(
                   (path.flags &
                    DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE) != 0)
            << L" sourceId="
            << path.sourceInfo.id
            << L" targetId="
            << path.targetInfo.id;

        if ((path.flags &
             DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE) != 0)
        {
            std::wcout
                << L" sourceMode="
                << path.sourceInfo.sourceModeInfoIdx
                << L" targetMode="
                << path.targetInfo.targetModeInfoIdx
                << L" cloneGroup="
                << path.sourceInfo.cloneGroupId
                << L" desktopMode="
                << path.targetInfo.desktopModeInfoIdx;
        }
        else
        {
            std::wcout
                << L" sourceMode="
                << path.sourceInfo.modeInfoIdx
                << L" targetMode="
                << path.targetInfo.modeInfoIdx;
        }

        std::wcout
            << std::endl;
    }

    return true;
}

bool DisplayManager::BuildTopology(
    std::vector<DISPLAYCONFIG_PATH_INFO>& paths,
    std::vector<DISPLAYCONFIG_MODE_INFO>& modes,
    std::wstring& error)
{
    constexpr int kMaxAttempts = 5;

    for (int attempt = 1;
         attempt <= kMaxAttempts;
         ++attempt)
    {
        if (BuildTopologyOnce(
                paths,
                modes,
                error))
        {
            return true;
        }

        if (attempt < kMaxAttempts)
        {
            Sleep(100);
        }
    }

    return false;
}

bool DisplayManager::SaveVddConfiguration(
    const std::vector<DISPLAYCONFIG_PATH_INFO>& paths,
    const std::vector<DISPLAYCONFIG_MODE_INFO>& modes)
{
    for (const auto& path : paths)
    {
        if ((path.flags &
             DISPLAYCONFIG_PATH_ACTIVE) == 0)
        {
            continue;
        }

        std::wstring friendly;
        std::wstring devicePath;

        GetTargetName(
            path,
            friendly,
            devicePath);

        DisplayPathInfo info;

        info.path = path;
        info.active = true;
        info.available = !friendly.empty();
        info.friendlyName = friendly;
        info.devicePath = devicePath;
        info.isVdd = IsVdd(info);

        if (!info.isVdd)
        {
            continue;
        }

        bool valid = false;

        if (IsVirtualAware(path))
        {
            if (path.sourceInfo.sourceModeInfoIdx !=
                DISPLAYCONFIG_PATH_SOURCE_MODE_IDX_INVALID &&
                path.sourceInfo.sourceModeInfoIdx <
                    modes.size())
            {
                valid = true;
            }

            if (path.targetInfo.targetModeInfoIdx !=
                DISPLAYCONFIG_PATH_TARGET_MODE_IDX_INVALID &&
                path.targetInfo.targetModeInfoIdx <
                    modes.size())
            {
                valid = true;
            }
        }
        else
        {
            if (path.sourceInfo.modeInfoIdx !=
                DISPLAYCONFIG_PATH_MODE_IDX_INVALID &&
                path.sourceInfo.modeInfoIdx <
                    modes.size())
            {
                valid = true;
            }

            if (path.targetInfo.modeInfoIdx !=
                DISPLAYCONFIG_PATH_MODE_IDX_INVALID &&
                path.targetInfo.modeInfoIdx <
                    modes.size())
            {
                valid = true;
            }
        }

        if (!valid)
        {
            continue;
        }

        // Preserve the original mode array exactly.
        savedVddPaths_.clear();
        savedVddModes_ = modes;

        savedVddPaths_.push_back(path);

        haveSavedVddConfig_ = true;

        std::wcout
            << L"[display] saved VDD configuration: "
            << friendly
            << L", modes="
            << savedVddModes_.size()
            << std::endl;

        return true;
    }

    return false;
}

bool DisplayManager::RestoreSavedVddConfiguration(
    std::vector<DISPLAYCONFIG_PATH_INFO>& paths,
    std::vector<DISPLAYCONFIG_MODE_INFO>& modes)
{
    if (!haveSavedVddConfig_ ||
        savedVddPaths_.empty() ||
        savedVddModes_.empty())
    {
        return false;
    }

    paths =
        savedVddPaths_;

    modes =
        savedVddModes_;

    for (auto& path : paths)
    {
        path.flags |=
            DISPLAYCONFIG_PATH_ACTIVE;
    }

    std::wcout
        << L"[display] restoring saved VDD configuration, "
        << L"paths="
        << paths.size()
        << L", modes="
        << modes.size()
        << std::endl;

    return true;
}

bool DisplayManager::Query(
    std::vector<DisplayPathInfo>& result,
    std::wstring& error)
{
    result.clear();
    error.clear();

    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;

    if (!BuildTopology(
            paths,
            modes,
            error))
    {
        return false;
    }

    result.reserve(paths.size());

    for (const auto& path : paths)
    {
        DisplayPathInfo info;

        info.path = path;

        info.active =
            (path.flags &
             DISPLAYCONFIG_PATH_ACTIVE) != 0;

        GetTargetName(
            path,
            info.friendlyName,
            info.devicePath);

        info.available =
            !info.friendlyName.empty();

        info.isVdd =
            IsVdd(info);

        result.push_back(info);
    }

    SaveVddConfiguration(
        paths,
        modes);

    return true;
}

bool DisplayManager::FindSourceMode(
    const DISPLAYCONFIG_PATH_INFO& path,
    const std::vector<DISPLAYCONFIG_MODE_INFO>& modes,
    UINT32& modeIndex) const
{
    modeIndex =
        DISPLAYCONFIG_PATH_MODE_IDX_INVALID;

    if (IsVirtualAware(path))
    {
        UINT32 index =
            path.sourceInfo.sourceModeInfoIdx;

        if (index !=
            DISPLAYCONFIG_PATH_SOURCE_MODE_IDX_INVALID &&
            index < modes.size())
        {
            modeIndex = index;
            return true;
        }

        return false;
    }

    UINT32 index =
        path.sourceInfo.modeInfoIdx;

    if (index !=
        DISPLAYCONFIG_PATH_MODE_IDX_INVALID &&
        index < modes.size())
    {
        modeIndex = index;
        return true;
    }

    return false;
}

bool DisplayManager::FindTargetMode(
    const DISPLAYCONFIG_PATH_INFO& path,
    const std::vector<DISPLAYCONFIG_MODE_INFO>& modes,
    UINT32& modeIndex) const
{
    modeIndex =
        DISPLAYCONFIG_PATH_MODE_IDX_INVALID;

    if (IsVirtualAware(path))
    {
        UINT32 index =
            path.targetInfo.targetModeInfoIdx;

        if (index !=
            DISPLAYCONFIG_PATH_TARGET_MODE_IDX_INVALID &&
            index < modes.size())
        {
            modeIndex = index;
            return true;
        }

        return false;
    }

    UINT32 index =
        path.targetInfo.modeInfoIdx;

    if (index !=
        DISPLAYCONFIG_PATH_MODE_IDX_INVALID &&
        index < modes.size())
    {
        modeIndex = index;
        return true;
    }

    return false;
}

bool DisplayManager::PreparePathModes(
    std::vector<DISPLAYCONFIG_PATH_INFO>& paths,
    const std::vector<DISPLAYCONFIG_MODE_INFO>& modes,
    DesiredMode mode,
    std::wstring& error)
{
    error.clear();

    if (mode == DesiredMode::VirtualOnly)
    {
        int vddIndex = -1;

        for (size_t i = 0;
             i < paths.size();
             ++i)
        {
            const auto& path = paths[i];

            std::wstring friendly;
            std::wstring devicePath;

            GetTargetName(
                path,
                friendly,
                devicePath);

            DisplayPathInfo info;

            info.path = path;
            info.active =
                (path.flags &
                 DISPLAYCONFIG_PATH_ACTIVE) != 0;
            info.friendlyName = friendly;
            info.devicePath = devicePath;
            info.available = !friendly.empty();
            info.isVdd = IsVdd(info);

            if (info.isVdd)
            {
                vddIndex =
                    static_cast<int>(i);

                break;
            }
        }

        if (vddIndex < 0)
        {
            error =
                L"No VDD path was found.";

            return false;
        }

        std::wcout
            << L"[display] VirtualOnly: VDD path index="
            << vddIndex
            << L" targetId="
            << paths[vddIndex].targetInfo.id
            << L" sourceId="
            << paths[vddIndex].sourceInfo.id
            << std::endl;

        for (auto& path : paths)
        {
            path.flags &=
                ~DISPLAYCONFIG_PATH_ACTIVE;

            InvalidatePathModes(path);
        }

        paths[vddIndex].flags |=
            DISPLAYCONFIG_PATH_ACTIVE;

        InvalidatePathModes(
            paths[vddIndex]);

        paths[vddIndex]
            .sourceInfo
            .cloneGroupId = 1;

        std::wcout
            << L"[display] VirtualOnly: selected VDD "
            << L"sourceId="
            << paths[vddIndex].sourceInfo.id
            << L" targetId="
            << paths[vddIndex].targetInfo.id
            << L" cloneGroup="
            << paths[vddIndex].sourceInfo.cloneGroupId
            << std::endl;

        return true;
    }

    struct PhysicalCandidate
    {
        size_t pathIndex;
        LUID adapterId;
        UINT32 targetId;
        UINT32 sourceId;
    };

    std::vector<PhysicalCandidate>
        candidates;

    std::wcout
        << L"[display] PhysicalOnly: scanning "
        << paths.size()
        << L" paths"
        << std::endl;

    for (size_t i = 0;
         i < paths.size();
         ++i)
    {
        const auto& path = paths[i];

        std::wstring friendly;
        std::wstring devicePath;

        GetTargetName(
            path,
            friendly,
            devicePath);

        DisplayPathInfo info;

        info.path = path;
        info.active =
            (path.flags &
             DISPLAYCONFIG_PATH_ACTIVE) != 0;
        info.friendlyName = friendly;
        info.devicePath = devicePath;
        info.available = !friendly.empty();
        info.isVdd = IsVdd(info);

        std::wcout
            << L"[display] candidate["
            << i
            << L"] name=["
            << friendly
            << L"] active="
            << (info.active ? L"true" : L"false")
            << L" available="
            << (info.available ? L"true" : L"false")
            << L" isVdd="
            << (info.isVdd ? L"true" : L"false")
            << L" targetAvailable="
            << (path.targetInfo.targetAvailable
                    ? L"true"
                    : L"false")
            << L" sourceId="
            << path.sourceInfo.id
            << L" targetId="
            << path.targetInfo.id
            << L" sourceMode="
            << path.sourceInfo.sourceModeInfoIdx
            << L" targetMode="
            << path.targetInfo.targetModeInfoIdx
            << std::endl;

        if (!path.targetInfo.targetAvailable)
        {
            continue;
        }

        if (!info.available || info.isVdd)
        {
            continue;
        }

        PhysicalCandidate candidate{};

        candidate.pathIndex = i;
        candidate.adapterId =
            path.targetInfo.adapterId;
        candidate.targetId =
            path.targetInfo.id;
        candidate.sourceId =
            path.sourceInfo.id;

        candidates.push_back(candidate);

        std::wcout
            << L"[display] candidate accepted: "
            << L"path="
            << i
            << L" targetId="
            << candidate.targetId
            << L" sourceId="
            << candidate.sourceId
            << std::endl;
    }

    if (candidates.empty())
    {
        error =
            L"No connected physical display path was found.";

        std::wcout
            << L"[display] PhysicalOnly: no candidates"
            << std::endl;

        return false;
    }

    std::vector<size_t> selectedIndices;
    std::vector<UINT32> usedSources;

    for (const auto& candidate : candidates)
    {
        bool targetAlreadySelected = false;

        for (size_t selected :
             selectedIndices)
        {
            const auto& selectedPath =
                paths[selected];

            if (selectedPath.targetInfo.adapterId.HighPart ==
                    candidate.adapterId.HighPart &&
                selectedPath.targetInfo.adapterId.LowPart ==
                    candidate.adapterId.LowPart &&
                selectedPath.targetInfo.id ==
                    candidate.targetId)
            {
                targetAlreadySelected = true;
                break;
            }
        }

        if (targetAlreadySelected)
        {
            std::wcout
                << L"[display] skip duplicate target: "
                << L"path="
                << candidate.pathIndex
                << L" targetId="
                << candidate.targetId
                << std::endl;

            continue;
        }

        bool sourceAlreadyUsed = false;

        for (UINT32 sourceId :
             usedSources)
        {
            if (sourceId ==
                candidate.sourceId)
            {
                sourceAlreadyUsed = true;
                break;
            }
        }

        if (sourceAlreadyUsed)
        {
            std::wcout
                << L"[display] skip used source: "
                << L"path="
                << candidate.pathIndex
                << L" sourceId="
                << candidate.sourceId
                << std::endl;

            continue;
        }

        selectedIndices.push_back(
            candidate.pathIndex);

        usedSources.push_back(
            candidate.sourceId);

        std::wcout
            << L"[display] selected physical path: "
            << L"path="
            << candidate.pathIndex
            << L" sourceId="
            << candidate.sourceId
            << L" targetId="
            << candidate.targetId
            << std::endl;
    }

    if (selectedIndices.empty())
    {
        error =
            L"No physical display path could be selected.";

        std::wcout
            << L"[display] PhysicalOnly: selection empty"
            << std::endl;

        return false;
    }

    for (auto& path : paths)
    {
        path.flags &=
            ~DISPLAYCONFIG_PATH_ACTIVE;

        InvalidatePathModes(path);
    }

    UINT32 cloneGroup = 1;

    for (size_t index :
         selectedIndices)
    {
        auto& path =
            paths[index];

        path.flags |=
            DISPLAYCONFIG_PATH_ACTIVE;

        InvalidatePathModes(path);

        path.sourceInfo.cloneGroupId =
            cloneGroup++;

        path.targetInfo.targetAvailable =
            TRUE;

        std::wcout
            << L"[display] prepared physical path: "
            << L"path="
            << index
            << L" sourceId="
            << path.sourceInfo.id
            << L" targetId="
            << path.targetInfo.id
            << L" sourceMode="
            << path.sourceInfo.sourceModeInfoIdx
            << L" targetMode="
            << path.targetInfo.targetModeInfoIdx
            << L" cloneGroup="
            << path.sourceInfo.cloneGroupId
            << std::endl;
    }

    std::wcout
        << L"[display] PhysicalOnly: selected="
        << selectedIndices.size()
        << std::endl;

    (void)modes;

    return true;
}

bool DisplayManager::Apply(
    DesiredMode mode,
    std::wstring& error)
{
    constexpr int kMaxAttempts = 5;

    for (int attempt = 1;
         attempt <= kMaxAttempts;
         ++attempt)
    {
        std::vector<DISPLAYCONFIG_PATH_INFO> paths;
        std::vector<DISPLAYCONFIG_MODE_INFO> modes;

        if (!BuildTopology(
                paths,
                modes,
                error))
        {
            if (attempt == kMaxAttempts)
            {
                return false;
            }

            Sleep(200);
            continue;
        }

        if (!PreparePathModes(
                paths,
                modes,
                mode,
                error))
        {
            return false;
        }

        std::vector<DISPLAYCONFIG_PATH_INFO>
            suppliedPaths;

        for (const auto& path : paths)
        {
            if (path.flags &
                DISPLAYCONFIG_PATH_ACTIVE)
            {
                suppliedPaths.push_back(path);
            }
        }

        if (suppliedPaths.empty())
        {
            error =
                L"No active paths were prepared.";

            return false;
        }

        std::wstringstream log;

        log << L"[display] apply attempt "
            << attempt
            << L"/"
            << kMaxAttempts
            << L"\n";

        log << L"[display] preparing topology, "
            << L"paths="
            << paths.size()
            << L", modes="
            << modes.size()
            << L", supplied="
            << suppliedPaths.size()
            << L"\n";

        for (size_t i = 0;
             i < suppliedPaths.size();
             ++i)
        {
            const auto& path =
                suppliedPaths[i];

            std::wstring friendly;
            std::wstring devicePath;

            GetTargetName(
                path,
                friendly,
                devicePath);

            log << L"[display] supplied["
                << i
                << L"] "
                << (friendly.empty()
                        ? L"<unknown>"
                        : friendly)
                << L" active="
                << BoolText(
                       (path.flags &
                        DISPLAYCONFIG_PATH_ACTIVE) != 0)
                << L" virtualAware="
                << BoolText(
                       IsVirtualAware(path))
                << L" flags=0x"
                << std::hex
                << path.flags
                << std::dec
                << L" sourceId="
                << path.sourceInfo.id
                << L" targetId="
                << path.targetInfo.id;

            if (IsVirtualAware(path))
            {
                log << L" sourceMode="
                    << path.sourceInfo.sourceModeInfoIdx
                    << L" targetMode="
                    << path.targetInfo.targetModeInfoIdx
                    << L" cloneGroup="
                    << path.sourceInfo.cloneGroupId
                    << L" desktopMode="
                    << path.targetInfo.desktopModeInfoIdx;
            }
            else
            {
                log << L" sourceMode="
                    << path.sourceInfo.modeInfoIdx
                    << L" targetMode="
                    << path.targetInfo.modeInfoIdx;
            }

            log << L"\n";
        }

        std::wcout << log.str();
        std::wcout.flush();

        UINT32 flags =
            SDC_APPLY |
            SDC_USE_SUPPLIED_DISPLAY_CONFIG |
            SDC_ALLOW_CHANGES |
            SDC_VIRTUAL_MODE_AWARE;

        std::wcout
            << L"[display] calling SetDisplayConfig..."
            << std::endl;

        LONG result =
            SetDisplayConfig(
                static_cast<UINT32>(
                    suppliedPaths.size()),
                suppliedPaths.data(),
                static_cast<UINT32>(
                    modes.size()),
                modes.empty()
                    ? nullptr
                    : modes.data(),
                flags);

        if (result == ERROR_SUCCESS)
        {
            std::wcout
                << L"[display] SetDisplayConfig succeeded."
                << std::endl;

            Sleep(500);

            if (Verify(
                    mode,
                    error))
            {
                return true;
            }

            std::wcout
                << L"[display] verification failed: "
                << error
                << std::endl;
        }
        else
        {
            error =
                LastErrorMessage(result);

            std::wcout
                << L"[display] SetDisplayConfig failed: "
                << error
                << std::endl;
        }

        if (attempt < kMaxAttempts)
        {
            Sleep(500);
        }
    }

    return false;
}

bool DisplayManager::Verify(
    DesiredMode mode,
    std::wstring& error)
{
    std::vector<DisplayPathInfo> paths;

    if (!Query(
            paths,
            error))
    {
        return false;
    }

    bool vddActive = false;
    bool physicalActive = false;

    for (const auto& path : paths)
    {
        if (!path.active)
        {
            continue;
        }

        if (path.isVdd)
        {
            vddActive = true;
        }
        else
        {
            physicalActive = true;
        }
    }

    if (mode == DesiredMode::VirtualOnly)
    {
        if (!vddActive)
        {
            error =
                L"Verification failed: VDD is not active.";

            return false;
        }

        if (physicalActive)
        {
            error =
                L"Verification failed: a physical display is still active.";

            return false;
        }

        return true;
    }

    if (!physicalActive)
    {
        error =
            L"Verification failed: no physical display is active.";

        return false;
    }

    if (vddActive)
    {
        error =
            L"Verification failed: VDD is still active.";

        return false;
    }

    return true;
}