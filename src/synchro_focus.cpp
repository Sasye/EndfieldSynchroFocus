#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#define APPLEPIE_PLUGIN_IMPL
#include "applepie_mgr.h"
#include <applepie_client.h>
#include "managed_input.h"
#include "focus_config.h"

static applepie::Client g_client;
static APH_RuntimeApi g_runtime{};
static APH_HookApi g_hooks{};
static ManagedApi g_managed;
static InputModel g_model;
static HANDLE g_cancel;
static volatile LONG g_workers, g_stopping, g_disabling, g_fault, g_ready, g_roots;
static uint64_t g_epoch;
static HWND g_window;
static DWORD g_gameThread;
static SRWLOCK g_controlLock = SRWLOCK_INIT;
static LONG AtomicRead(volatile LONG& value) { return InterlockedCompareExchange(&value, 0, 0); }

// Local evidence is the validated InputManager boundary on Unity's window
// thread. Never claim Core's single actor source or depend on EIEM being active.
static bool RuntimeOpen() {
    if (AtomicRead(g_stopping)) return false;
    if (g_runtime.probe_open) return g_runtime.probe_open(g_client.owner()) == APH_OK;
    APH_RuntimeSnapshot s{};
    return g_runtime.snapshot && g_runtime.snapshot(sizeof(s), &s) == APH_OK && s.state < APH_RUNTIME_STOPPING;
}
static bool GameThread() {
    return RuntimeOpen() && g_gameThread && GetCurrentThreadId() == g_gameThread &&
        IsWindow(g_window) && GetWindowThreadProcessId(g_window, nullptr) == g_gameThread &&
        g_managed.thread_current && g_managed.thread_current();
}
static void Fail(const char* reason) {
    if (!InterlockedExchange(&g_fault, 1)) {
        Log("[fault] %s; pending input/roots retained, restart required", reason);
        g_hooks.record_fault(g_client.owner(), 0, 0xe0415301u, 0, APH_ORIGINAL_NOT_ENTERED, 0);
    }
}
static uint32_t Root(void* object) {
    if (!object) return 0;
    uint32_t handle = g_managed.gchandle_new(object, false);
    if (handle) InterlockedIncrement(&g_roots);
    return handle;
}
static bool FreeRoot(uint32_t& handle) {
    if (handle) {
        if (!g_managed.gchandle_free.CallIfOpen(handle)) return false;
        handle = 0; InterlockedDecrement(&g_roots);
    }
    return true;
}
enum class ActionState { Idle, Pressing, Pressed, Releasing, Uncertain };
struct Held {
    uint32_t manager{}, pressInfo{}, endInfo{}, pressAction{}, endAction{};
    int pressId{}, endId{};
    uint64_t epoch{};
    ActionState state = ActionState::Idle;
};
static Held g_held[4]; // only the verified game thread reads/writes these
static bool g_keyDown[4];
static uint64_t g_appliedConfig, g_inputEpoch;
static thread_local bool g_checkpointThread;
static bool ClearHeld(Held& h) {
    if (!FreeRoot(h.pressAction) || !FreeRoot(h.endAction) || !FreeRoot(h.pressInfo) ||
        !FreeRoot(h.endInfo) || !FreeRoot(h.manager)) { h.state = ActionState::Uncertain; return false; }
    h = {}; return true;
}
static bool Invoke(void* action) {
    if (!GameThread() || !action || g_managed.object_get_class(action) != g_model.callbackClass) return false;
    // Match InputBindingInfo.Trigger's one-shot flags. The bool result is a
    // game decision, not an exception or permission to retry the callback.
    bool isRepeat = false, allowFailureDelay = false;
    void* arguments[]{&isRepeat, &allowFailureDelay};
    void* exception = nullptr;
    g_managed.runtime_invoke(g_model.invoke, action, g_model.callbackHasFlags ? arguments : nullptr, &exception);
    return exception == nullptr && RuntimeOpen(); // Native exceptions propagate; never replay.
}
static bool EndHeld(Held& h) {
    if (!h.manager) return true;
    // A release may synchronously delete its binding. It is already submitted;
    // the outer call owns its completion and root cleanup.
    if (h.state == ActionState::Releasing) return true;
    if (h.state != ActionState::Pressed || !GameThread()) return false;
    void* info = g_managed.gchandle_get_target(h.endInfo);
    void* callback = g_managed.gchandle_get_target(h.endAction);
    if (!info || !callback || g_managed.object_get_class(info) != g_model.infoClass ||
        ReadAt<int>(info, g_model.id) != h.endId || ReadAt<void*>(info, g_model.callback) != callback) return false;
    h.state = ActionState::Releasing;
    bool completed = false;
    __try { completed = Invoke(callback); }
    __finally { if (!completed) h.state = ActionState::Uncertain; }
    if (!completed) return false;
    return ClearHeld(h);
}
static bool EndAll() {
    bool ok = true;
    for (auto& held : g_held) if (!EndHeld(held)) ok = false;
    return ok;
}
static bool BeginHeld(void* self, int slot) {
    void* press = nullptr; void* end = nullptr;
    if (!g_model.FindPair(g_managed, self, slot, press, end)) { Fail("Input binding table changed layout"); return false; }
    if (!press || !end) return true; // A scene without battle bindings is normal.
    void* pressAction = ReadAt<void*>(press, g_model.callback);
    void* endAction = ReadAt<void*>(end, g_model.callback);
    if (!pressAction || !endAction || g_managed.object_get_class(pressAction) != g_model.callbackClass ||
        g_managed.object_get_class(endAction) != g_model.callbackClass) { Fail("Unexpected input callback type"); return false; }
    Held& held = g_held[slot];
    held.manager = Root(self); held.pressInfo = Root(press); held.endInfo = Root(end);
    held.pressAction = Root(pressAction); held.endAction = Root(endAction);
    if (!held.manager || !held.pressInfo || !held.endInfo || !held.pressAction || !held.endAction) {
        ClearHeld(held); Fail("GCHandle allocation failed before press"); return false;
    }
    held.pressId = ReadAt<int>(press, g_model.id); held.endId = ReadAt<int>(end, g_model.id);
    held.epoch = g_epoch; held.state = ActionState::Pressing;
    bool completed = false;
    __try { completed = Invoke(g_managed.gchandle_get_target(held.pressAction)); }
    __finally { if (!completed) held.state = ActionState::Uncertain; }
    if (!completed) { Fail("Press outcome unknown"); return false; }
    held.state = ActionState::Pressed;
    return true;
}
static bool Foreground() {
    HWND window = GetForegroundWindow();
    return window && GetAncestor(window, GA_ROOT) == GetAncestor(g_window, GA_ROOT);
}
static bool PhysicalDown(int key) { return key && (GetAsyncKeyState(key) & 0x8000) != 0; }
static void InputTick(void* self, const Config& config, const bool (&down)[4], bool focused) {
    if (g_inputEpoch != g_epoch || g_appliedConfig != config.generation) {
        if (!EndAll()) { Fail("Configuration/epoch change could not release old input"); return; }
        g_inputEpoch = g_epoch; g_appliedConfig = config.generation;
        for (int i = 0; i < 4; ++i) g_keyDown[i] = down[i];
        return; // Keys held across re-enable/config changes are not new presses.
    }
    for (int i = 0; i < 4 && !AtomicRead(g_fault); ++i) {
        Held& held = g_held[i];
        if (held.manager) {
            void* manager = g_managed.gchandle_get_target(held.manager);
            void* press = g_managed.gchandle_get_target(held.pressInfo);
            void* end = g_managed.gchandle_get_target(held.endInfo);
            const bool invalidated = manager != self || held.epoch != g_epoch || !press || !end ||
                ReadAt<void*>(press, g_model.callback) != g_managed.gchandle_get_target(held.pressAction) ||
                !g_model.Enabled(press) || !g_model.Enabled(end) ||
                ReadAt<void*>(press, g_model.group) != ReadAt<void*>(end, g_model.group);
            if ((!down[i] || !focused || !config.enabled || invalidated) && !EndHeld(held)) {
                Fail("Old input binding could not be released"); return;
            }
        }
        if (focused && config.enabled && down[i] && !g_keyDown[i] && !AtomicRead(g_disabling))
            if (!BeginHeld(self, i)) return;
        g_keyDown[i] = down[i];
    }
}

static applepie::Hook g_tick;
static APH_NativeProc g_tickOriginal;
using TickFunction = void(*)(void*, float, void*);
using DeleteHook = applepie::NativeHook<2, void(*)(void*, void*)>;
using ReleaseHook = applepie::NativeHook<3, void(*)(void*, void*)>;
static void Checkpoint() {
    const bool previous = g_checkpointThread; g_checkpointThread = true;
    __try { g_client.Checkpoint(); } __finally { g_checkpointThread = previous; }
}
static void TickEntry(void* self, float delta, void* method) {
    APH_HookCall call{}; uint32_t phase = APH_ORIGINAL_NOT_ENTERED;
    APH_Result entered = g_tick.api()->enter_call(g_tick.owner(), g_tick.handle(), &call);
    __try {
        __try {
            if (entered == APH_OK && (call.flags & APH_CALL_NATIVE_READY) &&
                !(call.flags & APH_CALL_RECURSIVE) && AtomicRead(g_ready) && GameThread()) {
                if (AtomicRead(g_disabling)) Checkpoint();
                if ((call.flags & APH_CALL_BUSINESS) && !AtomicRead(g_disabling) && !AtomicRead(g_fault)) {
                    const Config config = ReadConfig();
                    bool down[4]; for (int i = 0; i < 4; ++i) down[i] = PhysicalDown(config.keys[i]);
                    InputTick(self, config, down, Foreground());
                }
            }
            phase = APH_ORIGINAL_RUNNING;
            reinterpret_cast<TickFunction>(g_tickOriginal)(self, delta, method);
            phase = APH_ORIGINAL_RETURNED;
        } __except (APH_EntryFilter(g_tick.api(), g_tick.owner(), g_tick.handle(),
            GetExceptionInformation(), phase, APH_RECOVERY_NONE)) {}
    } __finally { if (entered == APH_OK) g_tick.api()->leave_call(g_tick.owner(), &call); }
}
static void OnDelete(void* self, void* method) {
    // Resident observation only finishes outstanding pairs; it never starts input.
    if (AtomicRead(g_roots) && !AtomicRead(g_fault)) {
        if (!GameThread()) Fail("Binding deleted outside the verified input thread");
        else {
            if (AtomicRead(g_disabling)) Checkpoint();
            for (auto& held : g_held) if (held.manager &&
                (g_managed.gchandle_get_target(held.pressInfo) == self ||
                 g_managed.gchandle_get_target(held.endInfo) == self))
                if (!EndHeld(held)) Fail("Binding deletion encountered unfinished input");
        }
    }
    DeleteHook::Original(self, method);
}
static void OnRelease(void* self, void* method) {
    if (AtomicRead(g_roots) && !AtomicRead(g_fault)) {
        if (!GameThread()) Fail("InputManager released outside the verified input thread");
        else {
            if (AtomicRead(g_disabling)) Checkpoint();
            for (auto& held : g_held) if (held.manager && g_managed.gchandle_get_target(held.manager) == self)
                if (!EndHeld(held)) Fail("InputManager release encountered unfinished input");
        }
    }
    ReleaseHook::Original(self, method);
}

static void StopRuntime() {
    InterlockedExchange(&g_stopping, 1);
    if (g_cancel) SetEvent(g_cancel);
}
#include "focus_stop_hooks.h"

static APH_Result APH_CALL Restore(APH_Owner, const APH_Cleanup*, void*) {
    if (AtomicRead(g_workers)) return APH_PENDING;
    if (AtomicRead(g_fault)) return APH_E_NEEDS_RESTART;
    if (!AtomicRead(g_roots)) { InterlockedExchange(&g_disabling, 0); return APH_OK; }
    if (AtomicRead(g_stopping)) return APH_E_NEEDS_RESTART;
    if (!g_checkpointThread || !GameThread()) return APH_PENDING;
    if (!EndAll()) return APH_E_NEEDS_RESTART;
    InterlockedExchange(&g_disabling, 0);
    return APH_OK;
}
static void APH_CALL Disabling(APH_Owner, uint64_t, void*) {
    AcquireSRWLockExclusive(&g_controlLock);
    __try { InterlockedExchange(&g_disabling, 1); if (g_cancel) SetEvent(g_cancel); }
    __finally { ReleaseSRWLockExclusive(&g_controlLock); }
}
static BOOL CALLBACK FindWindow(HWND window, LPARAM parameter) {
    DWORD process = 0; DWORD thread = GetWindowThreadProcessId(window, &process);
    wchar_t name[64];
    if (process == GetCurrentProcessId() && GetClassNameW(window, name, 64) &&
        !wcscmp(name, L"UnityWndClass") && !GetWindow(window, GW_OWNER)) {
        *reinterpret_cast<HWND*>(parameter) = window; g_gameThread = thread; return FALSE;
    }
    return TRUE;
}
struct EnableJob { uint64_t epoch; ULONGLONG deadline; };
static EnableJob g_job;
static bool CurrentJob(const EnableJob& job) {
    APH_OwnerStatus status{};
    return !AtomicRead(g_stopping) && WaitForSingleObject(g_cancel, 0) == WAIT_TIMEOUT &&
        g_client.Status(status) == APH_OK && status.state == APH_OWNER_ENABLING && status.activation_epoch == job.epoch;
}
static APH_Result WaitStep(const EnableJob& job, APH_Result (*step)()) {
    for (;;) {
        if (!CurrentJob(job)) return APH_E_STALE;
        const APH_Result r = step();
        if (r != APH_PENDING && r != APH_E_BUSY) return r;
        if (GetTickCount64() >= job.deadline) return APH_E_UNAVAILABLE;
        WaitForSingleObject(g_cancel, 5);
    }
}
static void LogHook(const char* stage, const char* name, APH_NativeProc target, APH_Result result, uint64_t claim) {
    if (!ReadConfig().debug) return;
    HMODULE module = nullptr;
    wchar_t path[MAX_PATH] = L"?";
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(target), &module)) GetModuleFileNameW(module, path, MAX_PATH);
    path[MAX_PATH - 1] = 0;
    const wchar_t* nameStart = wcsrchr(path, L'\\');
    unsigned char bytes[16]{}; SIZE_T read = 0;
    char byteText[49] = "unreadable";
    if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(target), bytes, sizeof(bytes), &read) && read == sizeof(bytes))
        for (size_t i = 0; i < sizeof(bytes); ++i)
            sprintf_s(byteText + i * 3, sizeof(byteText) - i * 3, "%02X%s", unsigned(bytes[i]), i + 1 == sizeof(bytes) ? "" : " ");
    Log("[hook] stage=%s name=%s result=%d claim=%llu target=%p module=%ls base=%p rva=0x%llX bytes=%s",
        stage, name, result, static_cast<unsigned long long>(claim), reinterpret_cast<void*>(target),
        nameStart ? nameStart + 1 : path, reinterpret_cast<void*>(module),
        static_cast<unsigned long long>(module ? reinterpret_cast<uintptr_t>(target) - reinterpret_cast<uintptr_t>(module) : 0), byteText);
}
static APH_Result WaitHook(const EnableJob& job, APH_Result (*step)(), const char* name, APH_NativeProc target, uint64_t claim) {
    const APH_Result r = WaitStep(job, step);
    LogHook("advance", name, target, r, claim);
    return r;
}
static APH_Result InstallShutdownBoundary(const EnableJob& job, APH_NativeProc target, bool& installed) {
    installed = false;
    if (!target) { Log("[stop] il2cpp_shutdown absent; ApplicationQuit is required"); return APH_OK; }
    APH_Result r = ShutdownHook::Request(target, "void() C-export", "il2cpp.runtime.shutdown");
    LogHook("request", "il2cpp_shutdown", target, r, ShutdownHook::hook.handle());
    // Core also rejects native export thunks. Only an unclaimed request may
    // fall back; errors after reservation/prepare/activation remain fatal.
    if (r == APH_E_FOREIGN_PATCH && !ShutdownHook::hook.handle()) {
        Log("[stop] il2cpp_shutdown rejected before claim; validated ApplicationQuit is required");
        return APH_OK;
    }
    if (r < 0) return r;
    r = WaitHook(job, ShutdownHook::Advance, "il2cpp_shutdown", target, ShutdownHook::hook.handle());
    installed = r == APH_OK;
    return r;
}
static APH_Result InstallQuitBoundary(const EnableJob& job, bool hasShutdown) {
    if (!g_model.quit) {
        Log("[stop] Validated ApplicationQuit absent; shutdown_active=%d", int(hasShutdown));
        return hasShutdown ? APH_OK : APH_E_UNAVAILABLE;
    }
    auto target = ReadAt<APH_NativeProc>(g_model.quit, 0);
    APH_Result r = QuitHook::Request(target, "void(MethodInfo*) static", "unity.Application.Internal_ApplicationQuit");
    LogHook("request", "Application.Internal_ApplicationQuit", target, r, QuitHook::hook.handle());
    if (r < 0) return r;
    return WaitHook(job, QuitHook::Advance, "Application.Internal_ApplicationQuit", target, QuitHook::hook.handle());
}
static APH_Result AdvanceTick() { return g_tick.Advance(&g_tickOriginal); }
static APH_Result InstallGameHooks(const EnableJob& job) {
    const auto tick = ReadAt<APH_NativeProc>(g_model.tick, 0);
    const auto onDelete = ReadAt<APH_NativeProc>(g_model.onDelete, 0);
    const auto release = ReadAt<APH_NativeProc>(g_model.release, 0);
    APH_HookRequest request{};
    APH_Result r = applepie::MakeHookRequest(request, 1, tick,
        reinterpret_cast<APH_NativeProc>(TickEntry), "void(self*,f32,MethodInfo*)", "synchro.InputManager.LateTick");
    if (r != APH_OK) return r;
    r = g_tick.Request(g_client, request);
    LogHook("request", "InputManager.LateTick", tick, r, g_tick.handle());
    if (r < 0) return r;
    applepie::NativeOptions options{}; options.module_observer = true; options.automatic_ready = false;
    r = DeleteHook::Request(g_client, 2, reinterpret_cast<DeleteHook::Function>(onDelete), OnDelete,
        "void(self*,MethodInfo*)", "synchro.InputBindingInfo.OnDelete", options);
    LogHook("request", "InputBindingInfo.OnDelete", onDelete, r, DeleteHook::handle());
    if (r < 0) return r;
    r = ReleaseHook::Request(g_client, 3, reinterpret_cast<ReleaseHook::Function>(release), OnRelease,
        "void(self*,MethodInfo*)", "synchro.InputManager.Release", options);
    LogHook("request", "InputManager.Release", release, r, ReleaseHook::handle());
    if (r < 0) return r;
    r = WaitHook(job, DeleteHook::Advance, "InputBindingInfo.OnDelete", onDelete, DeleteHook::handle()); if (r != APH_OK) return r;
    r = WaitHook(job, ReleaseHook::Advance, "InputManager.Release", release, ReleaseHook::handle()); if (r != APH_OK) return r;
    r = WaitHook(job, AdvanceTick, "InputManager.LateTick", tick, g_tick.handle()); if (r != APH_OK) return r;
    if (DeleteHook::PublishReady() != APH_OK || ReleaseHook::PublishReady() != APH_OK) return APH_E_UNAVAILABLE;
    InterlockedExchange(&g_ready, 1);
    return APH_OK;
}
static APH_Result InitializeManaged(const EnableJob& job, const char*& stage, bool hasShutdown) {
    stage = "wait-runtime-bootstrap";
    Log("[startup] Waiting for corlib and an existing registered VM thread before domain/attach/metadata");
    while (CurrentJob(job) && GetTickCount64() < job.deadline && !g_managed.WorkerAttachReady())
        WaitForSingleObject(g_cancel, 50);
    if (!CurrentJob(job) || GetTickCount64() >= job.deadline) return APH_E_UNAVAILABLE;
    Log("[startup] Worker readiness confirmed; starting attached metadata discovery");
    stage = "wait-domain";
    APH_Result r = APH_PENDING;
    void* attached = nullptr;
    __try {
        while (CurrentJob(job) && RuntimeOpen() && GetTickCount64() < job.deadline) {
            void* domain = g_managed.domain_get();
            if (!domain) { WaitForSingleObject(g_cancel, 50); continue; }
            stage = "thread-attach";
            if (!attached) attached = g_managed.thread_attach(domain);
            if (!attached) return APH_E_UNAVAILABLE;
            stage = "wait-input-assembly";
            size_t count = 0; void** assemblies = g_managed.domain_get_assemblies(domain, &count);
            if (!assemblies || !count || !g_managed.FindClass(assemblies, count, "Beyond.Input", "InputManager")) {
                WaitForSingleObject(g_cancel, 50); continue;
            }
            stage = "input-metadata";
            if (!g_model.Resolve(g_managed, assemblies, count)) {
                Log("[compat] Input metadata/ABI validation failed: %s", g_model.failure ? g_model.failure : "unknown");
                return APH_E_SIGNATURE;
            }
            Log("[compat] Input callback=%s; get_enabled bool() validated",
                g_model.callbackHasFlags ? "Func<bool,bool,bool>" : "Action");
            stage = "quit-boundary";
            r = InstallQuitBoundary(job, hasShutdown); if (r != APH_OK) return r;
            stage = "wait-Unity-window";
            while (CurrentJob(job) && GetTickCount64() < job.deadline && !g_window) {
                EnumWindows(FindWindow, reinterpret_cast<LPARAM>(&g_window));
                if (!g_window) WaitForSingleObject(g_cancel, 50);
            }
            if (!g_window || !CurrentJob(job)) return APH_E_UNAVAILABLE;
            stage = "input-hooks";
            return InstallGameHooks(job);
        }
    } __finally {
        if (attached && RuntimeOpen()) g_managed.thread_detach(attached);
    }
    return APH_E_UNAVAILABLE;
}
static APH_Result Initialize(const EnableJob& job, const char*& stage) {
    stage = "runtime-recheck";
    if (AtomicRead(g_ready)) return RuntimeOpen() ? APH_OK : APH_E_UNAVAILABLE;
    stage = "wait-GameAssembly";
    HMODULE module = nullptr;
    while (CurrentJob(job) && GetTickCount64() < job.deadline) {
        module = GetModuleHandleW(L"GameAssembly.dll");
        if (module) break;
        WaitForSingleObject(g_cancel, 50);
    }
    if (!module || !CurrentJob(job)) return APH_E_UNAVAILABLE;
    stage = "shutdown-boundary";
    bool hasShutdown = false;
    auto shutdown = reinterpret_cast<APH_NativeProc>(GetProcAddress(module, "il2cpp_shutdown"));
    APH_Result r = InstallShutdownBoundary(job, shutdown, hasShutdown);
    if (r != APH_OK) return r;
    stage = "IL2CPP-exports";
    const char* missing = nullptr;
    if (!g_managed.Load(module, &missing)) {
        Log("[compat] Required IL2CPP export missing: %s", missing ? missing : "unknown");
        return APH_E_UNAVAILABLE;
    }
    return InitializeManaged(job, stage, hasShutdown);
}
static DWORD WINAPI EnableWorker(void*) {
    const EnableJob job = g_job;
    const ULONGLONG started = GetTickCount64();
    const char* stage = "starting";
    __try {
        Log("[enable] version=0.0.6 epoch=%llu starting", static_cast<unsigned long long>(job.epoch));
        stage = "configuration";
        APH_Result result = APH_PENDING;
        while (CurrentJob(job) && GetTickCount64() < job.deadline) {
            result = SyncConfig();
            if (result != APH_PENDING) break;
            const ULONGLONG now = GetTickCount64();
            if (now >= job.deadline) break;
            const HANDLE signals[]{g_cancel, g_configChanged};
            if (WaitForMultipleObjects(2, signals, FALSE, DWORD(job.deadline - now)) != WAIT_OBJECT_0 + 1) {
                result = APH_E_UNAVAILABLE; break;
            }
        }
        if (!CurrentJob(job) || GetTickCount64() >= job.deadline) result = APH_E_UNAVAILABLE;
        if (result == APH_OK) result = Initialize(job, stage);
        // Complete even on runtime stop; Core rejects disabled/obsolete epochs.
        const APH_Result completion = g_client.CompleteEnable(job.epoch, result);
        Log("[enable] epoch=%llu stage=%s result=%d completion=%d elapsed_ms=%llu",
            static_cast<unsigned long long>(job.epoch), stage, result, completion,
            static_cast<unsigned long long>(GetTickCount64() - started));
    } __finally { InterlockedDecrement(&g_workers); }
    return 0;
}

extern "C" __declspec(dllexport) const APH_Manifest APH_PluginManifest = {
    sizeof(APH_Manifest), APH_ABI_MAJOR, APH_ABI_MINOR, APH_ARCH_X64,
    APH_PLUGIN_LIFECYCLE | APH_PLUGIN_LATE_LOAD | APH_PLUGIN_MODULE_OBSERVER,
    APH_CAP_CORE | APH_CAP_HOOKS, 0, "synchro_focus", "Synchro Focus"
};
extern "C" __declspec(dllexport) APH_Result APH_CALL APH_Attach(const APH_AttachArgs* args) {
    APH_Result r = g_client.Attach(args); if (r != APH_OK) return r;
    r = g_client.Service(g_runtime); if (r != APH_OK) return r;
    r = g_client.Service(g_hooks); if (r != APH_OK) return r;
    if (!SetPaths()) return APH_E_UNAVAILABLE;
    g_cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_cancel) return APH_E_UNAVAILABLE;
    const APH_LifecycleDesc life{sizeof(life), 60000, Disabling, Restore, nullptr};
    r = g_client.RegisterLifecycle(life);
    if (r == APH_OK) r = RegisterConfig(g_client);
    if (r != APH_OK) { CloseHandle(g_cancel); g_cancel = nullptr; }
    return r;
}
extern "C" __declspec(dllexport) APH_Result APH_CALL APH_Enable(APH_Owner owner, uint64_t epoch) {
    if (owner != g_client.owner() || !epoch) return APH_E_ARGUMENT;
    AcquireSRWLockExclusive(&g_controlLock);
    __try {
        APH_OwnerStatus status{};
        if (g_client.Status(status) != APH_OK || status.state != APH_OWNER_ENABLING || status.activation_epoch != epoch) return APH_E_STALE;
        if (AtomicRead(g_workers) || AtomicRead(g_roots)) return APH_E_BUSY;
        if (AtomicRead(g_stopping) || AtomicRead(g_fault)) return APH_E_NEEDS_RESTART;
        g_epoch = epoch; g_job = {epoch, GetTickCount64() + 45000};
        ResetEvent(g_cancel); InterlockedExchange(&g_disabling, 0); InterlockedIncrement(&g_workers);
        HANDLE thread = CreateThread(nullptr, 0, EnableWorker, nullptr, 0, nullptr);
        if (!thread) { InterlockedDecrement(&g_workers); return APH_E_UNAVAILABLE; }
        CloseHandle(thread); return APH_PENDING;
    } __finally { ReleaseSRWLockExclusive(&g_controlLock); }
}
BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
extern "C" __declspec(dllexport) void DummyExport() {}
static AP_PluginInfo g_info = {APPLEPIE_PLUGIN_API_VERSION, "synchro_focus", "Synchro Focus",
    "Bind ultimates to independent keys", "config/applepie.synchro_focus.ini", true};
APPLEPIE_PLUGIN_EXPORT AP_PluginInfo* AP_GetPluginInfo() { return &g_info; }
APPLEPIE_PLUGIN_EXPORT bool AP_PluginEnable() {
    APH_OwnerStatus s{}; return g_client.Status(s) == APH_OK && s.state == APH_OWNER_ACTIVE;
}
APPLEPIE_PLUGIN_EXPORT bool AP_PluginDisable() {
    const APH_Result result = g_client.Disable(); APH_OwnerStatus s{};
    if (g_client.Status(s) != APH_OK) return false;
    // Older Core builds only permit Disable from Active. The compatibility
    // cancellation below terminates as Failed when that limitation applies.
    if (result == APH_E_BUSY && s.state == APH_OWNER_ENABLING) {
        AcquireSRWLockExclusive(&g_controlLock);
        __try { if (g_cancel) SetEvent(g_cancel); }
        __finally { ReleaseSRWLockExclusive(&g_controlLock); }
    }
    return s.state == APH_OWNER_DISABLED;
}
APPLEPIE_PLUGIN_EXPORT bool AP_ReloadConfig() { return LoadConfig(); }
APPLEPIE_PLUGIN_EXPORT int AP_GetHotkeys(AP_HotkeyInfo* out, int capacity) {
    if (!out || capacity < 4) return 0;
    const Config c = ReadConfig();
    out[0] = {"Ultimate 1", "ult_1_key", c.keys[0]}; out[1] = {"Ultimate 2", "ult_2_key", c.keys[1]};
    out[2] = {"Ultimate 3", "ult_3_key", c.keys[2]}; out[3] = {"Ultimate 4", "ult_4_key", c.keys[3]};
    return 4;
}
