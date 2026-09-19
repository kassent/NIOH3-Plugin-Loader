#include <atomic>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>
#include <windows.h>
#include <DbgHelp.h>
#include <TlHelp32.h>
#include <winver.h>

#include <CommonUtils.h>
#include <FileUtils.h>
#include <HookUtils.h>
#include <LogUtils.h>
#include <PluginAPI.h>

extern "C" {
    HMODULE dll = nullptr;
    FARPROC OrignalDirectInput8Create = nullptr;
    FARPROC OrignalDllCanUnloadNow = nullptr;
    FARPROC OrignalDllGetClassObject = nullptr;
    FARPROC OrignalDllRegisterServer = nullptr;
    FARPROC OrignalDllUnregisterServer = nullptr;
    FARPROC OrignalGetdfDIJoystick = nullptr;

    void FakeDirectInput8Create();
    void FakeDllCanUnloadNow();
    void FakeDllGetClassObject();
    void FakeDllRegisterServer();
    void FakeDllUnregisterServer();
    void FakeGetdfDIJoystick();
}

#define PLUGIN_NAME "NIOH3PluginLoader"
#define PLUGIN_VERSION_MAJOR 1
#define PLUGIN_VERSION_MINOR 0
#define PLUGIN_VERSION_PATCH 2
namespace {

struct LoadedPlugin {
    HMODULE module = nullptr;
    std::wstring path;
};

struct VersionNumbers {
    uint16_t major = 0;
    uint16_t minor = 0;
    uint16_t patch = 0;
    uint16_t build = 0;
};

std::mutex g_plugin_mutex;
std::vector<LoadedPlugin> g_loaded_plugins;
std::once_flag g_plugin_load_once;
std::once_flag g_crash_handler_once;
std::atomic<bool> g_mutex_hook_disabled = false;
SafetyHookInline g_create_mutex_a_hook;
uint8_t **g_game_exception_handler = nullptr;

std::string g_game_version_string = "0.0.0.0";
std::string g_game_root_dir;
std::string g_plugins_dir;

constexpr DWORD STATUS_CONTROL_C_EXIT_CODE = 0xC000013A;
constexpr DWORD STATUS_FATAL_APP_EXIT_CODE = 0x40000015;
constexpr DWORD DBG_TERMINATE_PROCESS_CODE = 0x40010004;
constexpr DWORD DBG_CONTROL_C_CODE = 0x40010005;
constexpr DWORD DBG_CONTROL_BREAK_CODE = 0x40010008;

bool ShouldSkipCrashReport(DWORD exception_code) {
    switch (exception_code) {
    case STATUS_CONTROL_C_EXIT_CODE:
    case STATUS_FATAL_APP_EXIT_CODE:
    case DBG_TERMINATE_PROCESS_CODE:
    case DBG_CONTROL_C_CODE:
    case DBG_CONTROL_BREAK_CODE:
        return true;
    default:
        return false;
    }
}

bool TryGetModuleNameFromAddress(uintptr_t address, std::string& out_module_name, uintptr_t& out_module_base) {
    out_module_name.clear();
    out_module_base = 0;

    HMODULE module = nullptr;
    if (!GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(address),
        &module)) {
        return false;
    }

    out_module_base = reinterpret_cast<uintptr_t>(module);

    char module_path[MAX_PATH] = { 0 };
    if (GetModuleFileNameA(module, module_path, MAX_PATH) == 0) {
        out_module_base = 0;
        return false;
    }

    out_module_name = std::filesystem::path(module_path).filename().string();
    return !out_module_name.empty();
}

bool TryFormatAddressAsModuleOffset(uintptr_t address, std::string& out_formatted) {
    out_formatted.clear();

    std::string module_name;
    uintptr_t module_base = 0;
    if (!TryGetModuleNameFromAddress(address, module_name, module_base) || module_base == 0) {
        return false;
    }

    char out[128] = { 0 };
    std::snprintf(out, sizeof(out), "%s+0x%llX", module_name.c_str(),
        static_cast<unsigned long long>(address - module_base));
    out_formatted = out;
    return true;
}

void LogLoadedModules() {
    _MESSAGE("=== Module Base List ===");
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) {
        _MESSAGE("CreateToolhelp32Snapshot failed for module list.");
        return;
    }

    MODULEENTRY32W me {};
    me.dwSize = sizeof(me);
    if (!Module32FirstW(snapshot, &me)) {
        _MESSAGE("Module32FirstW failed.");
        CloseHandle(snapshot);
        return;
    }

    do {
        const auto module_name = CommonUtils::ConvertWStringToCString(me.szModule);
        const auto module_path = CommonUtils::ConvertWStringToCString(me.szExePath);
        _MESSAGE("  %s base=0x%p size=0x%08X path=%s",
            module_name.c_str(),
            me.modBaseAddr,
            me.modBaseSize,
            module_path.c_str());
    } while (Module32NextW(snapshot, &me));

    CloseHandle(snapshot);
}

void LogRegisters(const CONTEXT* ctx) {
    if (ctx == nullptr) {
        _MESSAGE("No CONTEXT available.");
        return;
    }


    _MESSAGE("=== Registers (x64) ===");
    _MESSAGE("RAX=0x%016llX RBX=0x%016llX RCX=0x%016llX RDX=0x%016llX",
        ctx->Rax, ctx->Rbx, ctx->Rcx, ctx->Rdx);
    _MESSAGE("RSI=0x%016llX RDI=0x%016llX RBP=0x%016llX RSP=0x%016llX",
        ctx->Rsi, ctx->Rdi, ctx->Rbp, ctx->Rsp);
    _MESSAGE("R8 =0x%016llX R9 =0x%016llX R10=0x%016llX R11=0x%016llX",
        ctx->R8, ctx->R9, ctx->R10, ctx->R11);
    _MESSAGE("R12=0x%016llX R13=0x%016llX R14=0x%016llX R15=0x%016llX",
        ctx->R12, ctx->R13, ctx->R14, ctx->R15);
    _MESSAGE("RIP=0x%016llX EFLAGS=0x%08lX", ctx->Rip, ctx->EFlags);
}

void LogCallStack(CONTEXT* original_ctx) {
    if (original_ctx == nullptr) {
        _MESSAGE("No context for stack walk.");
        return;
    }

    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();

    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    const BOOL sym_ok = SymInitialize(process, nullptr, TRUE);
    if (!sym_ok) {
        _MESSAGE("SymInitialize failed (err=%lu).", GetLastError());
    }

    CONTEXT ctx = *original_ctx;
    STACKFRAME64 frame {};
    DWORD machine = 0;


    machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrStack.Offset = ctx.Rsp;
    frame.AddrFrame.Offset = ctx.Rbp;

    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;

    _MESSAGE("=== Call Stack ===");
    for (uint32_t i = 0; i < 128; ++i) {
        const BOOL walked = StackWalk64(
            machine,
            process,
            thread,
            &frame,
            &ctx,
            nullptr,
            SymFunctionTableAccess64,
            SymGetModuleBase64,
            nullptr);

        if (!walked || frame.AddrPC.Offset == 0) {
            break;
        }

        const auto addr = static_cast<uintptr_t>(frame.AddrPC.Offset);
        std::string formatted;
        if (!TryFormatAddressAsModuleOffset(addr, formatted)) {
            continue;
        }
        _MESSAGE("  #%02u %s (0x%p)", i, formatted.c_str(), reinterpret_cast<void*>(addr));
    }

    if (sym_ok) {
        SymCleanup(process);
    }
}

LONG WINAPI UnhandledExceptionFilterCallback(EXCEPTION_POINTERS* exception_info) {
    if (exception_info != nullptr && exception_info->ExceptionRecord != nullptr) {
        const DWORD exception_code = exception_info->ExceptionRecord->ExceptionCode;
        if (ShouldSkipCrashReport(exception_code)) {
            _MESSAGE("Skipping crash report for exit-related exception code: 0x%08lX", exception_code);
            return EXCEPTION_CONTINUE_SEARCH;
        }
    }
    _MESSAGE("========================================");
    _MESSAGE("Unhandled exception captured.");

    if (exception_info != nullptr && exception_info->ExceptionRecord != nullptr) {
        const auto* er = exception_info->ExceptionRecord;

        // If we can't even resolve the exception address to a module, skip reporting.
        // This avoids generating noise during teardown / forced termination scenarios.
        std::string exception_addr_fmt;
        if (!TryFormatAddressAsModuleOffset(reinterpret_cast<uintptr_t>(er->ExceptionAddress), exception_addr_fmt)) {
            _MESSAGE("Skipping crash report: exception address is not inside a resolvable module. addr=0x%p", er->ExceptionAddress);
            return EXCEPTION_CONTINUE_SEARCH;
        }

        _MESSAGE("ExceptionCode: 0x%08lX", er->ExceptionCode);
        _MESSAGE("ExceptionFlags: 0x%08lX", er->ExceptionFlags);
        _MESSAGE("ExceptionAddress: %s (0x%p)", exception_addr_fmt.c_str(), er->ExceptionAddress);
    }

    if (exception_info != nullptr) {
        LogRegisters(exception_info->ContextRecord);
        LogCallStack(exception_info->ContextRecord);
    }

    LogLoadedModules();
    _MESSAGE("========================================");

    return EXCEPTION_EXECUTE_HANDLER;
}

void InstallCrashHandler() {
    std::call_once(g_crash_handler_once, []() {
        SetUnhandledExceptionFilter(UnhandledExceptionFilterCallback);
        _MESSAGE("SetUnhandledExceptionFilter installed.");
    });
}

std::string WidePathToUtf8(const std::filesystem::path& path) {
    const auto path_w = path.wstring();
    return CommonUtils::ConvertWStringToCString(path_w);
}

std::string BuildVersionString(const VersionNumbers& version) {
    return std::to_string(version.major) + "." +
        std::to_string(version.minor) + "." +
        std::to_string(version.patch) + "." +
        std::to_string(version.build);
}

bool QueryFileVersion(const std::filesystem::path& file_path, VersionNumbers& out_version) {
    using GetFileVersionInfoSizeW_t = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
    using GetFileVersionInfoW_t = BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
    using VerQueryValueW_t = BOOL(WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT);

    HMODULE version_dll = LoadLibraryW(L"version.dll");
    if (version_dll == nullptr) {
        return false;
    }

    const auto p_get_file_version_info_size = reinterpret_cast<GetFileVersionInfoSizeW_t>(
        GetProcAddress(version_dll, "GetFileVersionInfoSizeW"));
    const auto p_get_file_version_info = reinterpret_cast<GetFileVersionInfoW_t>(
        GetProcAddress(version_dll, "GetFileVersionInfoW"));
    const auto p_ver_query_value = reinterpret_cast<VerQueryValueW_t>(
        GetProcAddress(version_dll, "VerQueryValueW"));

    if (p_get_file_version_info_size == nullptr || p_get_file_version_info == nullptr || p_ver_query_value == nullptr) {
        FreeLibrary(version_dll);
        return false;
    }

    DWORD handle = 0;
    const auto file_path_w = file_path.wstring();
    const DWORD info_size = p_get_file_version_info_size(file_path_w.c_str(), &handle);
    if (info_size == 0) {
        FreeLibrary(version_dll);
        return false;
    }

    std::vector<uint8_t> file_info(info_size, 0);
    if (!p_get_file_version_info(file_path_w.c_str(), 0, info_size, file_info.data())) {
        FreeLibrary(version_dll);
        return false;
    }

    VS_FIXEDFILEINFO* fixed_info = nullptr;
    UINT fixed_info_size = 0;
    if (!p_ver_query_value(file_info.data(), L"\\", reinterpret_cast<LPVOID*>(&fixed_info), &fixed_info_size) || fixed_info == nullptr) {
        FreeLibrary(version_dll);
        return false;
    }

    out_version.major = HIWORD(fixed_info->dwFileVersionMS);
    out_version.minor = LOWORD(fixed_info->dwFileVersionMS);
    out_version.patch = HIWORD(fixed_info->dwFileVersionLS);
    out_version.build = LOWORD(fixed_info->dwFileVersionLS);
    FreeLibrary(version_dll);
    return true;
}

std::filesystem::path GetCurrentProcessExePath() {
    wchar_t exe_path[MAX_PATH] = { 0 };
    const DWORD len = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return {};
    }
    return std::filesystem::path(exe_path);
}

bool IsDllPath(const std::filesystem::path& path) {
    const auto ext = path.extension().wstring();
    return _wcsicmp(ext.c_str(), L".dll") == 0;
}

bool ResolveOriginalExports() {
    OrignalDirectInput8Create = GetProcAddress(dll, "DirectInput8Create");
    OrignalDllCanUnloadNow = GetProcAddress(dll, "DllCanUnloadNow");
    OrignalDllGetClassObject = GetProcAddress(dll, "DllGetClassObject");
    OrignalDllRegisterServer = GetProcAddress(dll, "DllRegisterServer");
    OrignalDllUnregisterServer = GetProcAddress(dll, "DllUnregisterServer");
    OrignalGetdfDIJoystick = GetProcAddress(dll, "GetdfDIJoystick");

    return OrignalDirectInput8Create != nullptr;
}

bool LoadOriginalDinput8() {
    char system_path[MAX_PATH] = { 0 };
    if (GetSystemDirectoryA(system_path, MAX_PATH) == 0) {
        return false;
    }

    const auto original_path = std::filesystem::path(system_path) / "dinput8.dll";
    dll = LoadLibraryA(original_path.string().c_str());
    if (dll == nullptr) {
        return false;
    }

    return ResolveOriginalExports();
}

void UnloadPlugins() {
    std::scoped_lock lock(g_plugin_mutex);
    for (auto it = g_loaded_plugins.rbegin(); it != g_loaded_plugins.rend(); ++it) {
        if (it->module != nullptr) {
            _MESSAGE("Unloading plugin: %s", CommonUtils::ConvertWStringToCString(it->path).c_str());
            FreeLibrary(it->module);
            it->module = nullptr;
        }
    }
    g_loaded_plugins.clear();
}

void LoadPlugins() {
    std::call_once(g_plugin_load_once, []() {
        const auto game_root = FileUtils::GetExecutableDirectory();
        const auto plugins_dir = game_root / "plugins";

        g_game_root_dir = game_root.string();
        g_plugins_dir = plugins_dir.string();

        _MESSAGE("Game root: %s", g_game_root_dir.c_str());
        _MESSAGE("Plugin root: %s", g_plugins_dir.c_str());

        if (!std::filesystem::exists(plugins_dir) || !std::filesystem::is_directory(plugins_dir)) {
            _MESSAGE("plugins directory does not exist, skip plugin loading.");
            return;
        }

        std::vector<std::filesystem::path> plugin_paths;
        for (const auto& entry : std::filesystem::directory_iterator(plugins_dir)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            if (!IsDllPath(entry.path())) {
                continue;
            }
            plugin_paths.push_back(entry.path());
        }

        std::sort(plugin_paths.begin(), plugin_paths.end());
        if (plugin_paths.empty()) {
            _MESSAGE("No plugin DLL found in plugins directory.");
            return;
        }

        const auto exe_path = GetCurrentProcessExePath();
        VersionNumbers version {};
        if (!exe_path.empty() && QueryFileVersion(exe_path, version)) {
            g_game_version_string = BuildVersionString(version);
        } else {
            g_game_version_string = "0.0.0.0";
        }

        Nioh3PluginInitializeParam initialize_param {};
        initialize_param.loader_api_version = NIOH3_PLUGIN_API_VERSION;
        initialize_param.game_version_major = version.major;
        initialize_param.game_version_minor = version.minor;
        initialize_param.game_version_patch = version.patch;
        initialize_param.game_version_build = version.build;
        initialize_param.game_version_string = g_game_version_string.c_str();
        initialize_param.game_root_dir = g_game_root_dir.c_str();
        initialize_param.plugins_dir = g_plugins_dir.c_str();

        _MESSAGE("Nioh3 plugin host initialized, game version: %s", initialize_param.game_version_string);
        _MESSAGE("Found %d plugin candidate(s).", static_cast<int>(plugin_paths.size()));

        for (const auto& plugin_path : plugin_paths) {
            const auto plugin_path_utf8 = WidePathToUtf8(plugin_path);
            _MESSAGE("Loading plugin: %s", plugin_path_utf8.c_str());

            HMODULE plugin_module = LoadLibraryW(plugin_path.c_str());
            if (plugin_module == nullptr) {
                _MESSAGE("Failed to load plugin: %s", plugin_path_utf8.c_str());
                continue;
            }

            bool initialized = false;
            const auto init_fn = reinterpret_cast<nioh3_plugin_initialize_fn>(
                GetProcAddress(plugin_module, "nioh3_plugin_initialize"));
            if (init_fn != nullptr) {
                try {
                    initialized = init_fn(&initialize_param);
                } catch (...) {
                    initialized = false;
                }
            } else {    
                _MESSAGE("Plugin has no nioh3_plugin_initialize export: %s", plugin_path_utf8.c_str());
                initialized = true;
            }

            if (!initialized) {
                _MESSAGE("Plugin initialize failed, unload: %s", plugin_path_utf8.c_str());
                FreeLibrary(plugin_module);
                continue;
            }

            {
                std::scoped_lock lock(g_plugin_mutex);
                g_loaded_plugins.push_back(LoadedPlugin { plugin_module, plugin_path.wstring() });
            }
            _MESSAGE("Plugin initialized: %s", plugin_path_utf8.c_str());
        }
    });
}

HANDLE WINAPI CreateMutexAHook(LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCSTR lpName) {
    HANDLE result = nullptr;
    if (g_create_mutex_a_hook) {
        result = g_create_mutex_a_hook.unsafe_call<HANDLE, LPSECURITY_ATTRIBUTES, BOOL, LPCSTR>(
            lpMutexAttributes, bInitialOwner, lpName);
    }

    if (!g_mutex_hook_disabled.load(std::memory_order_acquire) &&
        bInitialOwner == TRUE &&
        lpName != nullptr &&
        std::strcmp(lpName, "Nioh3_Mutex") == 0) {
        InstallCrashHandler();
        LoadPlugins();

        if (!g_mutex_hook_disabled.exchange(true, std::memory_order_acq_rel) && g_create_mutex_a_hook) {
            const auto disabled = g_create_mutex_a_hook.disable();
            if (!disabled.has_value()) {
                _MESSAGE("Failed to disable CreateMutexA trigger hook after match.");
            }
        }
    }
    return result;
}

bool InstallCreateMutexATriggerHook() {
    FARPROC create_mutex_addr = nullptr;

    if (const HMODULE kernelbase = GetModuleHandleW(L"KernelBase.dll"); kernelbase != nullptr) {
        create_mutex_addr = GetProcAddress(kernelbase, "CreateMutexA");
    }
    if (create_mutex_addr == nullptr) {
        if (const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll"); kernel32 != nullptr) {
            create_mutex_addr = GetProcAddress(kernel32, "CreateMutexA");
        }
    }

    if (create_mutex_addr == nullptr) {
        _MESSAGE("Failed to resolve CreateMutexA address.");
        return false;
    }

    g_create_mutex_a_hook = CreateHookFunction(
        reinterpret_cast<HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR)>(create_mutex_addr),
        &CreateMutexAHook);

    if (!g_create_mutex_a_hook) {
        _MESSAGE("Failed to install plugin loader trigger hook.");
        return false;
    }

    _MESSAGE("Plugin loader trigger hook installed at: %p", create_mutex_addr);
    return true;
}
} // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: {
        DisableThreadLibraryCalls(hModule);
        initLogger(PLUGIN_NAME);
        _MESSAGE("----------------------------------------");
        _MESSAGE("dinput8 proxy v%d.%d.%d loaded.", PLUGIN_VERSION_MAJOR, PLUGIN_VERSION_MINOR, PLUGIN_VERSION_PATCH);

        if (!LoadOriginalDinput8()) {
            MessageBoxA(nullptr, "Cannot load original dinput8.dll", PLUGIN_NAME, MB_ICONERROR);
            return FALSE;
        }

        if (!InstallCreateMutexATriggerHook()) {
            _MESSAGE("Plugin loader trigger hook install failed.");
        }

        break;
    }
    case DLL_PROCESS_DETACH: {
        if (lpReserved == nullptr) {
            UnloadPlugins();
            if (dll != nullptr) {
                FreeLibrary(dll);
                dll = nullptr;
            }
        }
        break;
    }
    default:
        break;
    }
    return TRUE;
}
