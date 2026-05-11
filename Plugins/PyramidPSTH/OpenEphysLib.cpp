#include "PyramidPSTH.h"

#include <PluginInfo.h>

#ifdef _WIN32
#include <Windows.h>
#define EXPORT __declspec (dllexport)
#else
#define EXPORT __attribute__ ((visibility ("default")))
#endif

using namespace Plugin;

#define NUM_PLUGINS 1

extern "C" EXPORT void getLibInfo (Plugin::LibraryInfo* info)
{
    info->apiVersion = PLUGIN_API_VER;
    info->name = "Pyramid PSTH";
    info->libVersion = ProjectInfo::versionString;
    info->numPlugins = NUM_PLUGINS;
}

extern "C" EXPORT int getPluginInfo (int index, Plugin::PluginInfo* info)
{
    switch (index)
    {
        case 0:
            info->type = Plugin::PROCESSOR;
            info->processor.name = "Pyramid PSTH";
            info->processor.type = Plugin::Processor::SINK;
            info->processor.creator = &(Plugin::createProcessor<PyramidPSTH>);
            break;
        default:
            return -1;
    }

    return 0;
}

#ifdef _WIN32
BOOL WINAPI DllMain (IN HINSTANCE hDllHandle,
                     IN DWORD nReason,
                     IN LPVOID Reserved)
{
    return TRUE;
}
#endif
