# NIOH3PluginLoader (dinput8 proxy mod loader)

## What this project does

`NIOH3PluginLoader` is a `dinput8.dll` proxy loader for **Nioh 3**.

It does three jobs:

1. Proxies calls to the original system `dinput8.dll` so the game still works normally.
2. Loads mod plugin DLLs from the game root `plugins` folder and initializes them through a shared plugin API.
3. Installs an unhandled-exception logger and prints crash details (exception code, registers, call stack, module base list) to `NIOH3PluginLoader.log`.

---

## For players (install / use)

1. Build or download this project's `dinput8.dll`.
2. Put `dinput8.dll` in the Nioh 3 game root directory (same folder as the game `.exe`).
3. Create a `plugins` directory in the game root if it does not exist.
4. Put mod plugin DLL files inside `plugins`.
5. Launch the game.

The loader scans `plugins/*.dll` and calls each plugin's `nioh3_plugin_initialize` export.

---

## For mod authors

### Required export

Every plugin DLL must export:

```cpp
extern "C" __declspec(dllexport)
bool nioh3_plugin_initialize(const Nioh3PluginInitializeParam* initialize_param);
```

If the export is missing or returns `false`, the plugin is unloaded.

### Shared API header

Use this header from the loader project:

- `include/Nioh3PluginAPI.h`

Current API:

```cpp
constexpr uint32_t NIOH3_PLUGIN_API_VERSION = 1;

struct Nioh3PluginInitializeParam {
    uint32_t loader_api_version;
    uint16_t game_version_major;
    uint16_t game_version_minor;
    uint16_t game_version_patch;
    uint16_t game_version_build;
    const char* game_version_string;
    const char* game_root_dir;
    const char* plugins_dir;
};
```

### Minimal plugin example

```cpp
#include "Nioh3PluginAPI.h"

extern "C" __declspec(dllexport)
bool nioh3_plugin_initialize(const Nioh3PluginInitializeParam* initialize_param) {
    if (initialize_param == nullptr) {
        return false;
    }

    // Optional: version gate
    if (initialize_param->loader_api_version != NIOH3_PLUGIN_API_VERSION) {
        return false;
    }

    // Initialize your hooks / systems here.
    return true;
}
```

### Plugin rules

- Build as `x64` DLL.
- Use `extern "C"` export name exactly: `nioh3_plugin_initialize`.
- Avoid expensive work in `DllMain`; do initialization in `nioh3_plugin_initialize`.

---

## Logging / troubleshooting

- Loader log file name: `NIOH3PluginLoader.log`
- Log location: Windows Documents folder

If your plugin does not load:

1. Confirm plugin DLL is in `<game root>/plugins`.
2. Confirm export name is exactly `nioh3_plugin_initialize`.
3. Check `NIOH3PluginLoader.log` for load / initialize failure messages.


### Crash / exception logging

The loader installs an unhandled-exception handler (via `SetUnhandledExceptionFilter`) after the early boot trigger
matches. When the game crashes, the loader prints crash details to the
loader log, including:

- Exception code / flags
- Exception address formatted as `module.dll+0xOFFSET`
- CPU registers (x64)
- Call stack (addresses formatted as `module.dll+0xOFFSET`)
- Loaded module base list (base address and size)



---

## Notes

- Plugin load order is alphabetical by file name.
- Each plugin is initialized once per process launch.
