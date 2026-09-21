#pragma once

#include <windows.h>
#include <vector>
#include <string>

struct DisplayPathInfo
{
    DISPLAYCONFIG_PATH_INFO path{};

    bool active = false;
    bool available = false;
    bool isVdd = false;

    std::wstring friendlyName;
    std::wstring devicePath;
};

enum class DesiredMode
{
    PhysicalOnly,
    VirtualOnly
};

class DisplayManager
{
public:
    explicit DisplayManager(std::wstring vddMatch);

    bool Query(
        std::vector<DisplayPathInfo>& paths,
        std::wstring& error);

    bool Apply(
        DesiredMode mode,
        std::wstring& error);

    bool IsVdd(
        const DisplayPathInfo& path) const;

    const std::wstring& GetVddMatch() const
    {
        return vddMatch_;
    }

private:
    std::wstring vddMatch_;

    std::vector<DISPLAYCONFIG_PATH_INFO> savedVddPaths_;
    std::vector<DISPLAYCONFIG_MODE_INFO> savedVddModes_;
    bool haveSavedVddConfig_ = false;

    static std::wstring ToLower(
        std::wstring value);

    static std::wstring LastErrorMessage(
        LONG error);

    bool GetTargetName(
        const DISPLAYCONFIG_PATH_INFO& path,
        std::wstring& friendly,
        std::wstring& devicePath);

    bool BuildTopology(
        std::vector<DISPLAYCONFIG_PATH_INFO>& paths,
        std::vector<DISPLAYCONFIG_MODE_INFO>& modes,
        std::wstring& error);

    bool BuildTopologyOnce(
        std::vector<DISPLAYCONFIG_PATH_INFO>& paths,
        std::vector<DISPLAYCONFIG_MODE_INFO>& modes,
        std::wstring& error);

    bool PreparePathModes(
        std::vector<DISPLAYCONFIG_PATH_INFO>& paths,
        const std::vector<DISPLAYCONFIG_MODE_INFO>& modes,
        DesiredMode mode,
        std::wstring& error);

    bool FindSourceMode(
        const DISPLAYCONFIG_PATH_INFO& path,
        const std::vector<DISPLAYCONFIG_MODE_INFO>& modes,
        UINT32& modeIndex) const;

    bool FindTargetMode(
        const DISPLAYCONFIG_PATH_INFO& path,
        const std::vector<DISPLAYCONFIG_MODE_INFO>& modes,
        UINT32& modeIndex) const;

    bool SaveVddConfiguration(
        const std::vector<DISPLAYCONFIG_PATH_INFO>& paths,
        const std::vector<DISPLAYCONFIG_MODE_INFO>& modes);

    bool RestoreSavedVddConfiguration(
        std::vector<DISPLAYCONFIG_PATH_INFO>& paths,
        std::vector<DISPLAYCONFIG_MODE_INFO>& modes);

    bool Verify(
        DesiredMode mode,
        std::wstring& error);
};