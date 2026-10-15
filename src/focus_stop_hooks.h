#pragma once
// Identical zero-byte contracts to EIEM. Native provider survives business stop.
template<unsigned Tag, class Signature> struct StopHook;
template<unsigned Tag, class... A> struct StopHook<Tag, void(*)(A...)> {
    inline static applepie::SharedHook hook;
    inline static applepie::Subscription observer;
    inline static APH_NativeProc original;
    static APH_Result APH_CALL Listen(APH_Owner, const APH_SharedEvent* event, void*) {
        if (event->phase == APH_BEFORE) StopRuntime();
        return APH_OK;
    }
    static int Fault(EXCEPTION_POINTERS* e, uint32_t phase) {
        hook.api()->native_fault(hook.owner(), hook.handle(), e->ExceptionRecord->ExceptionCode,
            uint64_t(reinterpret_cast<uintptr_t>(e->ExceptionRecord->ExceptionAddress)), phase);
        return EXCEPTION_CONTINUE_SEARCH;
    }
    static void Publish(const APH_SharedCall& call, uint32_t phase) {
        if ((call.flags & APH_SHARED_DISPATCH) && hook.api()->publish(&call, phase, nullptr, 0) < 0)
            RaiseException(0xe0415302u, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    }
    static void Entry(A... args) {
        APH_SharedCall call{}; uint32_t phase = APH_ORIGINAL_NOT_ENTERED;
        APH_Result entered = hook.api()->enter(hook.owner(), hook.handle(), &call);
        __try {
            __try {
                StopRuntime();
                if (entered == APH_OK) Publish(call, APH_BEFORE);
                phase = APH_ORIGINAL_RUNNING;
                reinterpret_cast<void(*)(A...)>(original)(args...);
                phase = APH_ORIGINAL_RETURNED;
                if (entered == APH_OK) Publish(call, APH_AFTER);
            } __except (Fault(GetExceptionInformation(), phase)) {}
        } __finally { if (entered == APH_OK) hook.api()->leave(&call); }
    }
    static APH_Result Request(APH_NativeProc target, const char* signature, const char* contract) {
        APH_SharedRequest request{};
        APH_Result r = applepie::MakeSharedRequest(request, Tag, target,
            reinterpret_cast<APH_NativeProc>(Entry), signature, contract, 1, 0);
        if (r != APH_OK) return r;
        r = hook.Request(g_client, request); if (r < 0) return r;
        if (!observer.handle()) {
            APH_SharedListen desc{sizeof(desc), APH_BEFORE, APH_MODULE_OBSERVER, APH_RECOVERY_NONE, Listen, nullptr};
            r = observer.Listen(hook, desc);
        }
        return r;
    }
    static APH_Result Advance() { return hook.Advance(&original); }
};
using ShutdownHook = StopHook<4, void(*)()>;
using QuitHook = StopHook<5, void(*)(void*)>;
