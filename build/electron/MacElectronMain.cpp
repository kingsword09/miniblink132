#include <stdio.h>
#include <string.h>

extern bool g_isElectronMode;

extern "C" void nodeModuleInitRegister(void);
extern "C" bool electronMacNodeBridgeHasLinkedModule(const char* name);

namespace {

bool hasArg(int argc, char** argv, const char* needle)
{
    for (int i = 1; i < argc; ++i) {
        if (argv[i] && strcmp(argv[i], needle) == 0)
            return true;
    }
    return false;
}

} // namespace

int main(int argc, char** argv)
{
    g_isElectronMode = true;
    nodeModuleInitRegister();

    if (hasArg(argc, argv, "--electron-native-theme-smoke")) {
        if (!electronMacNodeBridgeHasLinkedModule("electron_browser_native_theme")) {
            fprintf(stderr, "missing electron_browser_native_theme linked binding\n");
            return 2;
        }
        printf("PASS electron-native-theme-linked-binding\n");
    }

    if (hasArg(argc, argv, "--electron-power-monitor-smoke")) {
        if (!electronMacNodeBridgeHasLinkedModule("electron_browser_powermonitor")) {
            fprintf(stderr, "missing electron_browser_powermonitor linked binding\n");
            return 4;
        }
        printf("PASS electron-power-monitor-linked-binding\n");
    }

    if (hasArg(argc, argv, "--electron-power-save-blocker-smoke")) {
        if (!electronMacNodeBridgeHasLinkedModule("electron_browser_power_save_blocker")) {
            fprintf(stderr, "missing electron_browser_power_save_blocker linked binding\n");
            return 5;
        }
        printf("PASS electron-power-save-blocker-linked-binding\n");
    }

    if (hasArg(argc, argv, "--electron-screen-smoke")) {
        if (!electronMacNodeBridgeHasLinkedModule("electron_common_screen")) {
            fprintf(stderr, "missing electron_common_screen linked binding\n");
            return 3;
        }
        printf("PASS electron-screen-linked-binding\n");
    }

    return 0;
}
