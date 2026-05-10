#define _CRT_SECURE_NO_WARNINGS
#include "MinHook.h"
#include <fstream>
#include <string>
#include <windows.h>

extern "C" __declspec(dllexport) void DummyExport() {}

int g_enabled = 1;
int g_debugLog = 1;
int g_ult1_vk = '1', g_ult2_vk = '3', g_ult3_vk = '4', g_ult4_vk = '5';

void Log(const char *msg) {
  if (g_debugLog) {
    std::ofstream out("plugin\\synchro_focus_log.txt", std::ios::app);
    if (out.is_open()) { out << msg << std::endl; out.close(); }
  }
}

int CharToVK(char c) {
  if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
  return c;
}

void LoadConfig() {
  std::ifstream config("plugin\\synchro_focus_config.txt");
  if (!config.is_open()) return;
  std::string line;
  while (std::getline(config, line)) {
    if (line.empty() || line[0] == '#') continue;
    size_t pos = line.find('=');
    if (pos == std::string::npos) continue;
    std::string key = line.substr(0, pos), val = line.substr(pos + 1);
    while (!val.empty() && (val.back() == '\r' || val.back() == '\n' || val.back() == ' ')) val.pop_back();
    if (key == "enabled") g_enabled = std::stoi(val);
    else if (key == "debug_log") g_debugLog = std::stoi(val);
    else if (key == "ult_1_key" && !val.empty()) g_ult1_vk = CharToVK(val[0]);
    else if (key == "ult_2_key" && !val.empty()) g_ult2_vk = CharToVK(val[0]);
    else if (key == "ult_3_key" && !val.empty()) g_ult3_vk = CharToVK(val[0]);
    else if (key == "ult_4_key" && !val.empty()) g_ult4_vk = CharToVK(val[0]);
  }
}

static int SafeReadStr(void* strPtr, char* buf, int bufSz) {
  __try {
    int32_t len = *(int32_t*)((char*)strPtr + 16);
    if (len <= 0 || len > 1024) { buf[0] = 0; return -1; }
    wchar_t* chars = (wchar_t*)((char*)strPtr + 20);
    int j = 0;
    for (int i = 0; i < len && j < bufSz - 1; i++)
      buf[j++] = (chars[i] < 128) ? (char)chars[i] : '?';
    buf[j] = 0;
    return j;
  } __except(1) { buf[0] = 0; return -2; }
}

// --- Il2Cpp API ---
typedef void *(*Il2CppDomainGet)();
typedef void *(*Il2CppThreadAttach)(void *domain);
typedef void *(*Il2CppGetImage)(void *assembly);
typedef void *(*Il2CppDomainGetAssemblies)(void *domain, size_t *size);
typedef void *(*Il2CppClassFromName)(void *image, const char *ns, const char *name);
typedef void *(*Il2CppClassGetMethods)(void *klass, void **iter);
typedef const char* (*Il2CppMethodGetName)(void *method);
typedef uint32_t (*Il2CppMethodGetParamCount)(void *method);
typedef void* (*Il2CppRuntimeInvoke)(void *method, void *obj, void **params, void **exc);
typedef void* (*Il2CppObjectGetClass)(void *obj);

Il2CppDomainGet il2cpp_domain_get;
Il2CppThreadAttach il2cpp_thread_attach;
Il2CppDomainGetAssemblies il2cpp_domain_get_assemblies;
Il2CppClassFromName il2cpp_class_from_name;
Il2CppClassGetMethods il2cpp_class_get_methods;
Il2CppMethodGetName il2cpp_method_get_name;
Il2CppMethodGetParamCount il2cpp_method_get_param_count;
Il2CppGetImage il2cpp_assembly_get_image;
Il2CppRuntimeInvoke il2cpp_runtime_invoke;
Il2CppObjectGetClass il2cpp_object_get_class;

HMODULE hGameAssembly = NULL;

void ResolveIl2Cpp() {
  hGameAssembly = GetModuleHandleW(L"GameAssembly.dll");
  if (!hGameAssembly) return;
  il2cpp_domain_get = (Il2CppDomainGet)GetProcAddress(hGameAssembly, "il2cpp_domain_get");
  il2cpp_thread_attach = (Il2CppThreadAttach)GetProcAddress(hGameAssembly, "il2cpp_thread_attach");
  il2cpp_domain_get_assemblies = (Il2CppDomainGetAssemblies)GetProcAddress(hGameAssembly, "il2cpp_domain_get_assemblies");
  il2cpp_class_from_name = (Il2CppClassFromName)GetProcAddress(hGameAssembly, "il2cpp_class_from_name");
  il2cpp_class_get_methods = (Il2CppClassGetMethods)GetProcAddress(hGameAssembly, "il2cpp_class_get_methods");
  il2cpp_method_get_name = (Il2CppMethodGetName)GetProcAddress(hGameAssembly, "il2cpp_method_get_name");
  il2cpp_method_get_param_count = (Il2CppMethodGetParamCount)GetProcAddress(hGameAssembly, "il2cpp_method_get_param_count");
  il2cpp_assembly_get_image = (Il2CppGetImage)GetProcAddress(hGameAssembly, "il2cpp_assembly_get_image");
  il2cpp_runtime_invoke = (Il2CppRuntimeInvoke)GetProcAddress(hGameAssembly, "il2cpp_runtime_invoke");
  il2cpp_object_get_class = (Il2CppObjectGetClass)GetProcAddress(hGameAssembly, "il2cpp_object_get_class");
}

struct Il2CppMethodInfo {
  void *methodPointer;
  void *invoker_method;
  const char *name;
  void *klass;
  const void *return_type;
  const void *parameters;
};

// --- Captured Action delegates ---
void* g_ultPressAction[4] = {};
void* g_ultReleaseAction[4] = {};
bool g_keyWasDown[4] = {};
bool g_captureLogged = false;

// Invoke System.Action via il2cpp_runtime_invoke on Action.Invoke()
void* g_actionInvokeMethod = NULL; // cached Invoke MethodInfo

void ResolveActionInvoke(void* actionObj) {
  if (g_actionInvokeMethod || !actionObj) return;
  void* klass = il2cpp_object_get_class(actionObj);
  if (!klass) return;
  void* iter = NULL;
  void* method;
  while ((method = il2cpp_class_get_methods(klass, &iter)) != NULL) {
    if (strcmp(il2cpp_method_get_name(method), "Invoke") == 0 
        && il2cpp_method_get_param_count(method) == 0) {
      g_actionInvokeMethod = method;
      Log("Resolved Action.Invoke()");
      return;
    }
  }
  Log("Failed to find Action.Invoke()");
}

// SEH-safe wrapper for il2cpp_runtime_invoke (no C++ objects with dtors)
static int SafeInvokeAction(void* invokeMethod, void* actionObj) {
  __try {
    void* exc = NULL;
    il2cpp_runtime_invoke(invokeMethod, actionObj, NULL, &exc);
    if (exc) return -1; // managed exception
    return 0; // success
  } __except(1) {
    return -2; // SEH exception (delegate likely GC'd)
  }
}

void InvokeAction(void* actionObj) {
  if (!actionObj) return;
  if (!g_actionInvokeMethod) ResolveActionInvoke(actionObj);
  if (!g_actionInvokeMethod) return;
  int result = SafeInvokeAction(g_actionInvokeMethod, actionObj);
  if (result == -1) Log("Exception in Action.Invoke()");
  else if (result == -2) Log("SEH: delegate may have been GC'd, ignoring.");
}

// --- Hook LateTick to run on Unity main thread ---
typedef void (*tLateTick)(void* __this, void* arg1, void* methodInfo);
tLateTick oLateTick = NULL;

// SEH-safe wrapper for the entire LateTick hook body
static void LateTick_KeyCheck() {
  int vks[4] = { g_ult1_vk, g_ult2_vk, g_ult3_vk, g_ult4_vk };
  
  for (int i = 0; i < 4; i++) {
    bool isDown = (GetAsyncKeyState(vks[i]) & 0x8000) != 0;
    
    if (isDown && !g_keyWasDown[i]) {
      if (g_ultPressAction[i]) InvokeAction(g_ultPressAction[i]);
    } else if (!isDown && g_keyWasDown[i]) {
      if (g_ultReleaseAction[i]) InvokeAction(g_ultReleaseAction[i]);
    }
    g_keyWasDown[i] = isDown;
  }
}

void hkLateTick(void* __this, void* arg1, void* methodInfo) {
  LateTick_KeyCheck();
  oLateTick(__this, arg1, methodInfo);
}

// --- Hook CreateBindingByActionId to capture callbacks ---
typedef void* (*tCreateBindingByActionId)(void* __this, void* actionId, void* callback, int priority, void* methodInfo);
tCreateBindingByActionId oCreateBindingByActionId = NULL;

void* hkCreateBindingByActionId(void* __this, void* actionId, void* callback, int priority, void* methodInfo) {
  void* result = oCreateBindingByActionId(__this, actionId, callback, priority, methodInfo);
  
  char buf[256];
  if (SafeReadStr(actionId, buf, sizeof(buf)) <= 0) return result;
  
  if (strstr(buf, "battle_use_char_skill_alt_")) {
    bool isEnd = (strstr(buf, "_end_") != NULL);
    int slot = -1;
    if (strstr(buf, "_alt_1") || strstr(buf, "_alt_end_1")) slot = 0;
    else if (strstr(buf, "_alt_2") || strstr(buf, "_alt_end_2")) slot = 1;
    else if (strstr(buf, "_alt_3") || strstr(buf, "_alt_end_3")) slot = 2;
    else if (strstr(buf, "_alt_4") || strstr(buf, "_alt_end_4")) slot = 3;
    
    if (slot >= 0) {
      if (isEnd) g_ultReleaseAction[slot] = callback;
      else g_ultPressAction[slot] = callback;
      
      if (!g_captureLogged) {
        Log((std::string("[Captured] ") + buf + " slot=" + std::to_string(slot) 
             + (isEnd ? " (release)" : " (press)")).c_str());
      }
    }
  }
  return result;
}

// Version compatibility: expected param counts for hooked methods
#define EXPECTED_LATETICK_PARAMS 1
#define EXPECTED_CREATEBINDING_PARAMS 3

bool HookMethods() {
  if (!il2cpp_domain_get) { Log("[Compat] il2cpp_domain_get not resolved"); return false; }
  void *domain = il2cpp_domain_get();
  il2cpp_thread_attach(domain);

  size_t size = 0;
  void **assemblies = (void **)il2cpp_domain_get_assemblies(domain, &size);

  bool foundInputMgr = false;
  bool hookedLateTick = false;
  bool hookedBinding = false;

  for (size_t i = 0; i < size; i++) {
    void *image = il2cpp_assembly_get_image(assemblies[i]);
    void *klass = il2cpp_class_from_name(image, "Beyond.Input", "InputManager");
    if (!klass) continue;
    foundInputMgr = true;

    void *iter = NULL;
    void *method;
    // Track LateTick param count for version compatibility check
    int lateTickParamCount = -1;
    int bindingParamCount = -1;

    while ((method = il2cpp_class_get_methods(klass, &iter)) != NULL) {
      const char* name = il2cpp_method_get_name(method);
      uint32_t count = il2cpp_method_get_param_count(method);
      
      if (strcmp(name, "CreateBindingByActionId") == 0 && count == EXPECTED_CREATEBINDING_PARAMS) {
        Il2CppMethodInfo *mInfo = (Il2CppMethodInfo *)method;
        if (mInfo->methodPointer) {
          MH_STATUS st = MH_CreateHook(mInfo->methodPointer, &hkCreateBindingByActionId, (LPVOID*)&oCreateBindingByActionId);
          if (st == MH_OK) {
            MH_EnableHook(mInfo->methodPointer);
            hookedBinding = true;
            Log("Hooked CreateBindingByActionId");
          } else {
            Log(("[Compat] MH_CreateHook CreateBindingByActionId failed: " + std::to_string(st)).c_str());
          }
        }
        bindingParamCount = count;
      }
      
      if (strcmp(name, "LateTick") == 0) {
        lateTickParamCount = count;
        if (count == EXPECTED_LATETICK_PARAMS) {
          Il2CppMethodInfo *mInfo = (Il2CppMethodInfo *)method;
          if (mInfo->methodPointer) {
            MH_STATUS st = MH_CreateHook(mInfo->methodPointer, &hkLateTick, (LPVOID*)&oLateTick);
            if (st == MH_OK) {
              MH_EnableHook(mInfo->methodPointer);
              hookedLateTick = true;
              Log("Hooked LateTick OK");
            } else {
              Log(("[Compat] MH_CreateHook LateTick failed: " + std::to_string(st)).c_str());
            }
          }
        } else {
          Log(("[Compat] WARNING: LateTick has " + std::to_string(count) 
               + " params, expected " + std::to_string(EXPECTED_LATETICK_PARAMS)
               + ". Game version may be incompatible. Skipping hook.").c_str());
        }
      }
    }
    break;
  }

  // Version compatibility report
  if (!foundInputMgr) {
    Log("[Compat] FATAL: Beyond.Input.InputManager not found. Game version incompatible.");
    return false;
  }
  if (!hookedLateTick) {
    Log("[Compat] FATAL: Failed to hook LateTick. Plugin will not function.");
    return false;
  }
  if (!hookedBinding) {
    Log("[Compat] FATAL: Failed to hook CreateBindingByActionId. Plugin will not function.");
    return false;
  }
  return true;
}

void Setup() {
  HANDLE hMutex = CreateMutexA(NULL, TRUE, "Local\\EndfieldSynchroFocus_InstanceGuard");
  if (GetLastError() == ERROR_ALREADY_EXISTS) { if (hMutex) CloseHandle(hMutex); return; }

  remove("plugin\\synchro_focus_log.txt");
  LoadConfig();
  Log("SynchroFocus v1.0 loaded.");
  Log((std::string("Keys: ult1=") + std::to_string(g_ult1_vk) + " ult2=" + std::to_string(g_ult2_vk)
    + " ult3=" + std::to_string(g_ult3_vk) + " ult4=" + std::to_string(g_ult4_vk)).c_str());

  if (!g_enabled) { Log("Disabled via config."); return; }

  MH_Initialize();
  Log("Waiting for GameAssembly...");
  while (!GetModuleHandleW(L"GameAssembly.dll")) Sleep(1000);
  Sleep(5000);

  Log("Resolving Il2Cpp API...");
  ResolveIl2Cpp();
  
  // Validate critical Il2Cpp exports
  if (!il2cpp_domain_get || !il2cpp_runtime_invoke || !il2cpp_object_get_class) {
    Log("[Compat] FATAL: Critical Il2Cpp exports missing. Cannot proceed.");
    return;
  }

  Log("Hooking InputManager...");
  bool success = HookMethods();
  if (success) {
    Log("Setup complete. All hooks active.");
  } else {
    Log("Setup FAILED. Plugin inactive due to compatibility issues.");
    // Disable hooks if partially installed
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
  }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(hModule);
    CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)Setup, NULL, 0, NULL);
  }
  return TRUE;
}
