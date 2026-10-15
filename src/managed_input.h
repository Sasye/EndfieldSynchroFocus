#pragma once
// Read-only metadata and input-table access. No hard-coded game offsets/RVAs.
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <type_traits>

static bool RuntimeOpen();
template<class> struct RuntimeFunction;
template<class R, class... A> struct RuntimeFunction<R(*)(A...)> {
    using Function = R(*)(A...);
    Function pointer = nullptr;
    RuntimeFunction& operator=(Function p) { pointer = p; return *this; }
    explicit operator bool() const { return pointer != nullptr; }
    R operator()(A... args) const {
        if (pointer && RuntimeOpen()) return pointer(args...);
        if constexpr (!std::is_void_v<R>) return R{};
    }
    bool CallIfOpen(A... args) const {
        if (!pointer || !RuntimeOpen()) return false;
        pointer(args...); return true;
    }
};

struct ManagedApi {
#define SF_API(R, N, ...) RuntimeFunction<R(*)(__VA_ARGS__)> N
    SF_API(void*, get_corlib);
    SF_API(void**, thread_get_all_attached_threads, size_t*);
    SF_API(void*, domain_get);
    SF_API(void*, thread_attach, void*);
    SF_API(void, thread_detach, void*);
    SF_API(void*, thread_current);
    SF_API(void**, domain_get_assemblies, void*, size_t*);
    SF_API(void*, assembly_get_image, void*);
    SF_API(void*, class_from_name, void*, const char*, const char*);
    SF_API(void*, class_get_methods, void*, void**);
    SF_API(const char*, method_get_name, void*);
    SF_API(uint32_t, method_get_param_count, void*);
    SF_API(uint32_t, method_get_flags, void*, uint32_t*);
    SF_API(bool, method_is_generic, void*);
    SF_API(bool, method_is_inflated, void*);
    SF_API(void*, method_get_param, void*, uint32_t);
    SF_API(void*, method_get_return_type, void*);
    SF_API(int, type_get_type, void*);
    SF_API(bool, type_is_byref, void*);
    SF_API(void*, class_from_type, void*);
    SF_API(bool, class_is_valuetype, void*);
    SF_API(const char*, class_get_name, void*);
    SF_API(const char*, class_get_namespace, void*);
    SF_API(void*, class_get_field_from_name, void*, const char*);
    SF_API(void*, field_get_type, void*);
    SF_API(int, field_get_flags, void*);
    SF_API(size_t, field_get_offset, void*);
    SF_API(void*, class_get_element_class, void*);
    SF_API(int32_t, class_value_size, void*, uint32_t*);
    SF_API(uint32_t, object_header_size);
    SF_API(uint32_t, array_object_header_size);
    SF_API(uintptr_t, array_length, void*);
    SF_API(int32_t, array_element_size, void*);
    SF_API(void*, object_get_class, void*);
    SF_API(int32_t, string_length, void*);
    SF_API(const wchar_t*, string_chars, void*);
    SF_API(void*, runtime_invoke, void*, void*, void**, void**);
    SF_API(uint32_t, gchandle_new, void*, bool);
    SF_API(void*, gchandle_get_target, uint32_t);
    SF_API(void, gchandle_free, uint32_t);
#undef SF_API
    bool Load(HMODULE module, const char** missing = nullptr) {
        if (missing) *missing = nullptr;
#define SF_LOAD(N) N = reinterpret_cast<decltype(N)::Function>(GetProcAddress(module, "il2cpp_" #N)); \
        if (!N) { if (missing) *missing = "il2cpp_" #N; return false; }
        SF_LOAD(get_corlib); SF_LOAD(thread_get_all_attached_threads);
        SF_LOAD(domain_get); SF_LOAD(thread_attach); SF_LOAD(thread_detach); SF_LOAD(thread_current);
        SF_LOAD(domain_get_assemblies); SF_LOAD(assembly_get_image); SF_LOAD(class_from_name);
        SF_LOAD(class_get_methods); SF_LOAD(method_get_name); SF_LOAD(method_get_param_count);
        SF_LOAD(method_get_flags); SF_LOAD(method_is_generic); SF_LOAD(method_is_inflated);
        SF_LOAD(method_get_param); SF_LOAD(method_get_return_type); SF_LOAD(type_get_type);
        SF_LOAD(type_is_byref); SF_LOAD(class_from_type); SF_LOAD(class_is_valuetype);
        SF_LOAD(class_get_name); SF_LOAD(class_get_namespace); SF_LOAD(class_get_field_from_name);
        SF_LOAD(field_get_type); SF_LOAD(field_get_flags); SF_LOAD(field_get_offset);
        SF_LOAD(class_get_element_class); SF_LOAD(class_value_size); SF_LOAD(object_header_size);
        SF_LOAD(array_object_header_size); SF_LOAD(array_length); SF_LOAD(array_element_size);
        SF_LOAD(object_get_class); SF_LOAD(string_length); SF_LOAD(string_chars);
        SF_LOAD(runtime_invoke); SF_LOAD(gchandle_new); SF_LOAD(gchandle_get_target); SF_LOAD(gchandle_free);
#undef SF_LOAD
        return true;
    }
    bool WorkerAttachReady() const {
        // Domain::GetCurrent can allocate through GC. Do not call it until GC
        // and Thread initialization have published corlib and a VM thread.
        if (!get_corlib || !thread_get_all_attached_threads || !get_corlib()) return false;
        size_t count = 0;
        // The returned array is borrowed and mutable; no entry is dereferenced.
        return thread_get_all_attached_threads(&count) && count != 0;
    }
    int Kind(void* type) const { return type && !type_is_byref(type) ? type_get_type(type) : -1; }
    void* FindClass(void** assemblies, size_t count, const char* ns, const char* name) const {
        void* result = nullptr;
        for (size_t i = 0; i < count; ++i) {
            void* image = assembly_get_image(assemblies[i]);
            void* candidate = image ? class_from_name(image, ns, name) : nullptr;
            if (candidate) { if (result && result != candidate) return nullptr; result = candidate; }
        }
        return result;
    }
    // Hook methods must not be inflated. A closed generic delegate Invoke may
    // be inflated only when explicitly requested and every concrete type matches.
    void* Method(void* klass, const char* name, bool isStatic, int resultKind,
                 uint32_t count = 0, int parameterKind = 0, bool allowInflated = false) const {
        if (!klass) return nullptr;
        void* found = nullptr;
        void* iterator = nullptr;
        while (void* method = class_get_methods(klass, &iterator)) {
            const char* actual = method_get_name(method);
            if (!actual || strcmp(actual, name)) continue;
            uint32_t impl = 0, flags = method_get_flags(method, &impl);
            if (bool(flags & 0x10) != isStatic || (flags & 0x400) ||
                method_is_generic(method) || (!allowInflated && method_is_inflated(method)) ||
                method_get_param_count(method) != count ||
                Kind(method_get_return_type(method)) != resultKind) continue;
            bool parametersMatch = true;
            for (uint32_t i = 0; i < count; ++i)
                if (Kind(method_get_param(method, i)) != parameterKind) parametersMatch = false;
            if (!parametersMatch) continue;
            if (found) return nullptr;
            found = method;
        }
        return found;
    }
    void* Field(void* klass, const char* name, int kind, void* exactClass = nullptr) const {
        void* f = klass ? class_get_field_from_name(klass, name) : nullptr;
        if (!f || (field_get_flags(f) & 0x10)) return nullptr;
        void* type = field_get_type(f);
        if (Kind(type) != kind || (exactClass && class_from_type(type) != exactClass)) return nullptr;
        return f;
    }
    bool Named(void* klass, const char* ns, const char* name) const {
        if (!klass) return false;
        const char* actualNamespace = class_get_namespace(klass);
        const char* actualName = class_get_name(klass);
        return actualNamespace && actualName && !strcmp(actualNamespace, ns) && !strcmp(actualName, name);
    }
};

template<class T> static T ReadAt(void* object, size_t offset) {
    T value{};
    memcpy(&value, static_cast<const char*>(object) + offset, sizeof(value));
    return value;
}

struct InputModel {
    void *managerClass{}, *infoClass{}, *callbackClass{}, *dictionaryClass{}, *arrayClass{};
    void *tick{}, *onDelete{}, *release{}, *quit{}, *invoke{}, *enabled{};
    RuntimeFunction<bool(*)(void*, void*)> getEnabled;
    size_t bindings{}, entries{}, count{}, id{}, callback{}, group{}, action{};
    size_t entryHash{}, entryKey{}, entryValue{};
    uint32_t arrayHeader{};
    int stride{};
    bool callbackHasFlags{};
    const char* failure{};

    bool Reject(const char* reason) { failure = reason; return false; }
    bool Resolve(const ManagedApi& a, void** assemblies, size_t assemblyCount) {
        *this = {};
        managerClass = a.FindClass(assemblies, assemblyCount, "Beyond.Input", "InputManager");
        infoClass = a.FindClass(assemblies, assemblyCount, "Beyond.Input", "InputBindingInfo");
        void* application = a.FindClass(assemblies, assemblyCount, "UnityEngine", "Application");
        if (!managerClass || a.class_is_valuetype(managerClass)) return Reject("Beyond.Input.InputManager class");
        if (!infoClass || a.class_is_valuetype(infoClass)) return Reject("Beyond.Input.InputBindingInfo class");
        tick = a.Method(managerClass, "LateTick", false, 1, 1, 0x0c);
        onDelete = a.Method(infoClass, "OnDelete", false, 1);
        release = a.Method(managerClass, "Release", false, 1);
        quit = a.Method(application, "Internal_ApplicationQuit", true, 1);
        enabled = a.Method(infoClass, "get_enabled", false, 2);
        if (!tick) return Reject("InputManager.LateTick: expected instance void(float)");
        if (!onDelete) return Reject("InputBindingInfo.OnDelete: expected instance void()");
        if (!release) return Reject("InputManager.Release: expected instance void()");
        if (!enabled) return Reject("InputBindingInfo.get_enabled: expected instance bool()");
        getEnabled = ReadAt<decltype(getEnabled)::Function>(enabled, 0);
        if (!getEnabled) return Reject("InputBindingInfo.get_enabled native entry");
        void* f = a.class_get_field_from_name(infoClass, "m_callback");
        if (!f || (a.field_get_flags(f) & 0x10)) return Reject("InputBindingInfo.m_callback instance field");
        void* callbackType = a.field_get_type(f);
        callbackClass = a.class_from_type(callbackType);
        if (!callbackClass || a.class_is_valuetype(callbackClass)) return Reject("InputBindingInfo.m_callback reference type");
        if (a.Kind(callbackType) == 0x12 && a.Named(callbackClass, "System", "Action")) {
            invoke = a.Method(callbackClass, "Invoke", false, 1);
        } else if (a.Kind(callbackType) == 0x15 && a.Named(callbackClass, "System", "Func`3")) {
            callbackHasFlags = true;
            invoke = a.Method(callbackClass, "Invoke", false, 2, 2, 2, true);
        }
        if (!invoke) return Reject("InputBindingInfo.m_callback: expected Action() or Func<bool,bool,bool>");
        callback = a.field_get_offset(f);
        f = a.Field(managerClass, "m_bindingInfos", 0x15);
        if (!f) return Reject("InputManager.m_bindingInfos Dictionary field");
        bindings = a.field_get_offset(f);
        dictionaryClass = a.class_from_type(a.field_get_type(f));
        if (!a.Named(dictionaryClass, "System.Collections.Generic", "Dictionary`2")) return Reject("InputManager.m_bindingInfos Dictionary type");
        f = a.Field(dictionaryClass, "_entries", 0x1d);
        if (!f) return Reject("Dictionary._entries array field");
        entries = a.field_get_offset(f);
        arrayClass = a.class_from_type(a.field_get_type(f));
        void* element = a.class_get_element_class(arrayClass);
        uint32_t alignment = 0;
        stride = element ? a.class_value_size(element, &alignment) : 0;
        arrayHeader = a.array_object_header_size();
        const size_t objectHeader = a.object_header_size();
        if (!element || !a.class_is_valuetype(element) || stride < 16 || stride > 128 ||
            a.array_element_size(arrayClass) != stride || objectHeader != 16 || arrayHeader < 24 || arrayHeader > 64)
            return Reject("Dictionary entry/array layout");
#define SF_FIELD(OUT, CLASS, NAME, KIND, EXACT) \
        f = a.Field(CLASS, NAME, KIND, EXACT); if (!f) return Reject(#CLASS "." NAME); OUT = a.field_get_offset(f)
        SF_FIELD(count, dictionaryClass, "_count", 8, nullptr);
        SF_FIELD(entryHash, element, "hashCode", 8, nullptr);
        SF_FIELD(entryKey, element, "key", 8, nullptr);
        SF_FIELD(entryValue, element, "value", 0x12, infoClass);
        if (entryHash < objectHeader || entryKey < objectHeader || entryValue < objectHeader) return Reject("Dictionary boxed entry offsets");
        entryHash -= objectHeader; entryKey -= objectHeader; entryValue -= objectHeader;
        if (entryHash + 4 > size_t(stride) || entryKey + 4 > size_t(stride) || entryValue + 8 > size_t(stride))
            return Reject("Dictionary entry field bounds");
        SF_FIELD(id, infoClass, "<id>k__BackingField", 8, nullptr);
        SF_FIELD(group, infoClass, "<group>k__BackingField", 0x12, nullptr);
        SF_FIELD(action, infoClass, "playerActionId", 0x0e, nullptr);
#undef SF_FIELD
        if (!(bindings >= objectHeader && entries >= objectHeader && count >= objectHeader &&
            id >= objectHeader && callback >= objectHeader && group >= objectHeader &&
            action >= objectHeader)) return Reject("Input instance field offsets");
        return true;
    }
    bool Enabled(void* info) const {
        // Use the game's full rule (including parent/guide overrides) through
        // a validated, cached bool() entry. No metadata scan or boxed bool.
        return info && getEnabled(info, enabled);
    }
    bool ActionEquals(const ManagedApi& a, void* str, int slot, bool end) const {
        if (!str) return false;
        const wchar_t* prefix = end ? L"battle_use_char_skill_alt_end_" : L"battle_use_char_skill_alt_";
        const size_t prefixLength = wcslen(prefix);
        if (a.string_length(str) != int(prefixLength + 1)) return false;
        const wchar_t* chars = a.string_chars(str);
        return chars && !wmemcmp(chars, prefix, prefixLength) && chars[prefixLength] == L'1' + slot;
    }
    struct View { void* array{}; int size{}; };
    bool ViewOf(const ManagedApi& a, void* self, View& v) const {
        if (!self || a.object_get_class(self) != managerClass) return false;
        void* dict = ReadAt<void*>(self, bindings);
        if (!dict) { v = {}; return true; }
        if (a.object_get_class(dict) != dictionaryClass) return false;
        v.size = ReadAt<int>(dict, count); v.array = ReadAt<void*>(dict, entries);
        return v.size >= 0 && v.size <= 65536 && (!v.size ||
            (v.array && a.object_get_class(v.array) == arrayClass && uintptr_t(v.size) <= a.array_length(v.array)));
    }
    void* At(const ManagedApi& a, const View& v, int index) const {
        void* entry = static_cast<char*>(v.array) + arrayHeader + size_t(index) * stride;
        if (ReadAt<int>(entry, entryHash) < 0) return nullptr;
        void* info = ReadAt<void*>(entry, entryValue);
        if (!info || a.object_get_class(info) != infoClass ||
            ReadAt<int>(info, id) != ReadAt<int>(entry, entryKey)) return nullptr;
        return info;
    }
    // Scan only on a rising key edge; steady frames do not walk the dictionary.
    bool FindPair(const ManagedApi& a, void* self, int slot, void*& press, void*& end) const {
        View v{}; press = end = nullptr;
        if (!ViewOf(a, self, v)) return false;
        for (int i = 0; i < v.size; ++i) {
            void* info = At(a, v, i);
            if (Enabled(info) &&
                ActionEquals(a, ReadAt<void*>(info, action), slot, false) &&
                (!press || ReadAt<int>(info, id) > ReadAt<int>(press, id))) press = info;
        }
        if (!press) return true;
        for (int i = 0; i < v.size; ++i) {
            void* info = At(a, v, i);
            if (Enabled(info) && ReadAt<void*>(info, group) == ReadAt<void*>(press, group) &&
                ActionEquals(a, ReadAt<void*>(info, action), slot, true) &&
                (!end || ReadAt<int>(info, id) > ReadAt<int>(end, id))) end = info;
        }
        return true;
    }
};
