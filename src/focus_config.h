#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cerrno>

struct Config { int enabled = 1, debug = 1, keys[4] = {'1', '3', '4', '5'}; uint64_t generation = 1; };
static Config g_config;
static SRWLOCK g_configLock = SRWLOCK_INIT, g_logLock = SRWLOCK_INIT;
static wchar_t g_configPath[MAX_PATH], g_logPath[MAX_PATH], g_basicConfigPath[MAX_PATH];
static APH_ConfigApi g_configApi{};
static APH_Owner g_configOwner;
static SRWLOCK g_configSyncLock = SRWLOCK_INIT;
static HANDLE g_configChanged;
static bool g_configImportPending;
static Config ReadConfig() {
    Config result;
    AcquireSRWLockShared(&g_configLock);
    __try { result = g_config; } __finally { ReleaseSRWLockShared(&g_configLock); }
    return result;
}
static void Log(const char* format, ...) {
    if (!ReadConfig().debug) return;
    char message[768];
    va_list args; va_start(args, format); vsnprintf(message, sizeof(message), format, args); va_end(args);
    OutputDebugStringA(message); OutputDebugStringA("\n");
    AcquireSRWLockExclusive(&g_logLock);
    __try {
        FILE* file = _wfopen(g_logPath, L"ab");
        if (file) { fprintf(file, "%s\n", message); fclose(file); }
    } __finally { ReleaseSRWLockExclusive(&g_logLock); }
}
static char* Trim(char* s) {
    while (*s == ' ' || *s == '\t') ++s;
    char* end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) *--end = 0;
    return s;
}
static bool ParseNumber(const char* s, int& value, bool key) {
    if (!s || !*s) return false;
    if (key && !s[1]) {
        const unsigned char c = static_cast<unsigned char>(*s);
        value = c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
        return value > 0 && value <= 255;
    }
    errno = 0; char* end = nullptr;
    long n = strtol(s, &end, s[0] == '0' && (s[1] == 'x' || s[1] == 'X') ? 16 : 10);
    if (errno || end == s || *end || n < 0 || n > (key ? 255 : 1)) return false;
    value = int(n); return true;
}
static bool LoadLegacyConfig() {
    Config next = ReadConfig();
    FILE* file = _wfopen(g_configPath, L"rb");
    if (!file) return errno == ENOENT;
    char line[512]; bool valid = true;
    while (fgets(line, sizeof(line), file)) {
        char* key = Trim(line);
        if (strlen(key) >= 3 && !memcmp(key, "\xef\xbb\xbf", 3)) key += 3;
        if (!*key || *key == '#') continue;
        char* equals = strchr(key, '='); if (!equals) continue;
        *equals = 0; char* value = Trim(equals + 1); key = Trim(key);
        int* output = nullptr; bool vk = false;
        if (!strcmp(key, "enabled")) output = &next.enabled;
        else if (!strcmp(key, "debug_log")) output = &next.debug;
        else for (int i = 0; i < 4; ++i) {
            char name[24]; snprintf(name, sizeof(name), "ult_%d_key", i + 1);
            if (!strcmp(key, name)) { output = &next.keys[i]; vk = true; break; }
        }
        if (output && !ParseNumber(value, *output, vk)) { valid = false; break; }
    }
    if (ferror(file)) valid = false;
    fclose(file);
    if (!valid) { Log("[config] Invalid value; previous configuration retained"); return false; }
    AcquireSRWLockExclusive(&g_configLock);
    __try { next.generation = g_config.generation + 1; g_config = next; }
    __finally { ReleaseSRWLockExclusive(&g_configLock); }
    return true;
}
static bool SetPaths() {
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&SetPaths), &self)) return false;
    wchar_t path[MAX_PATH]; DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
    if (!n || n >= MAX_PATH) return false;
    wchar_t* slash = wcsrchr(path, L'\\'); if (!slash) return false; *++slash = 0;
    return swprintf_s(g_configPath, L"%ssynchro_focus_config.txt", path) > 0 &&
        swprintf_s(g_logPath, L"%ssynchro_focus_log.txt", path) > 0 &&
        swprintf_s(g_basicConfigPath, L"%sconfig\\applepie.synchro_focus.ini", path) > 0;
}

// Registration/import runs once during Attach. Runtime reads are memory-only;
// the input callback releases captured old bindings when generation changes.
static APH_Result SyncConfig() {
    if (!g_configApi.snapshot) return APH_E_UNAVAILABLE;
    AcquireSRWLockExclusive(&g_configSyncLock);
    __try {
        APH_ConfigSnapshot state{};
        APH_ConfigValue values[6]{};
        APH_Result r = g_configApi.snapshot(g_configOwner, sizeof(state), &state, 6, values);
        if (r != APH_OK) return r;
        if (g_configImportPending) {
            if (state.pending) return APH_PENDING;
            if (state.last_result != APH_OK) return state.last_result;
            g_configImportPending = false;
        }
        Config next{}; next.enabled = values[0].integer; next.debug = values[1].integer;
        for (int i = 0; i < 4; ++i) next.keys[i] = values[i + 2].integer;
        AcquireSRWLockExclusive(&g_configLock);
        __try {
            const bool inputChanged = next.enabled != g_config.enabled || memcmp(next.keys, g_config.keys, sizeof(next.keys));
            next.generation = g_config.generation + (inputChanged ? 1 : 0);
            g_config = next;
        } __finally { ReleaseSRWLockExclusive(&g_configLock); }
        return APH_OK;
    } __finally { ReleaseSRWLockExclusive(&g_configSyncLock); }
}
static bool LoadConfig() { return SyncConfig() == APH_OK; }
static void APH_CALL ConfigChanged(APH_Owner, void*) {
    // Only plugin-owned POD is updated here. InputTick applies the generation
    // change and releases old input on the verified game thread.
    SyncConfig();
    SetEvent(g_configChanged);
}

static APH_Result RegisterConfig(const applepie::Client& client) {
    APH_Result r = client.Service(g_configApi);
    if (r != APH_OK) return r;
    if (!g_configApi.set_notifier) return APH_E_UNAVAILABLE;
    g_configOwner = client.owner();
    const DWORD attributes = GetFileAttributesW(g_basicConfigPath);
    const DWORD pathError = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
    if (pathError && pathError != ERROR_FILE_NOT_FOUND && pathError != ERROR_PATH_NOT_FOUND) return APH_E_IO;
    bool importLegacy = false;
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD legacy = GetFileAttributesW(g_configPath);
        const DWORD error = legacy == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
        if (error && error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) return APH_E_IO;
        if (legacy != INVALID_FILE_ATTRIBUTES) {
            if (!LoadLegacyConfig()) return APH_E_ARGUMENT;
            importLegacy = true;
        }
    }
    APH_ConfigField fields[6]{};
    const char* keys[]{"enabled", "debug_log", "ult_1_key", "ult_2_key", "ult_3_key", "ult_4_key"};
    const char* labels[]{"Enabled / 启用功能", "Debug log / 调试日志", "Ultimate 1 / 终结技 1", "Ultimate 2 / 终结技 2", "Ultimate 3 / 终结技 3", "Ultimate 4 / 终结技 4"};
    const Config defaults{};
    for (unsigned i = 0; i < 6; ++i) {
        auto& f = fields[i]; f.size = sizeof(f); f.type = f.initial.type = i < 2 ? APH_CONFIG_BOOL : APH_CONFIG_KEY;
        strcpy_s(f.key, keys[i]); strcpy_s(f.label, labels[i]);
        f.initial.integer = i < 2 ? 1 : defaults.keys[i - 2];
        if (i >= 2) {
            strcpy_s(f.description, "Click and press a key; Clear disables it / 点击后按键，清除可禁用");
        }
    }
    const APH_ConfigSchema schema{sizeof(schema), 6, fields};
    r = g_configApi.register_schema(g_configOwner, &schema);
    if (r != APH_OK) return r;
    g_configChanged = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!g_configChanged) return APH_E_UNAVAILABLE;
    r = g_configApi.set_notifier(g_configOwner, ConfigChanged, nullptr);
    if (r != APH_OK) { CloseHandle(g_configChanged); g_configChanged = nullptr; return r; }
    // Notification context/event remain resident across logical disable.
    if (importLegacy) {
        const Config imported = ReadConfig();
        APH_ConfigSnapshot state{}; APH_ConfigValue values[6]{};
        r = g_configApi.snapshot(g_configOwner, sizeof(state), &state, 6, values);
        if (r != APH_OK) return r;
        values[0].integer = imported.enabled; values[1].integer = imported.debug;
        for (int i = 0; i < 4; ++i) values[i + 2].integer = imported.keys[i];
        AcquireSRWLockExclusive(&g_configSyncLock);
        g_configImportPending = true;
        ReleaseSRWLockExclusive(&g_configSyncLock);
        r = g_configApi.save(g_configOwner, state.revision, 6, values);
        if (r != APH_PENDING && r != APH_OK) return r;
        Log("[config] Importing legacy settings into Manager configuration");
        return APH_OK; // Enable worker waits for the durable import, cancellably.
    }
    return SyncConfig();
}
