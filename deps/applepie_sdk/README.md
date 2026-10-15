# Vendored Applepie SDK headers

These unmodified public headers were copied from the local ApplepieManager/sdk
on 2026-10-09. They provide ABI 1.1 and Config v1 with key fields and optional
completion notifications. The source SDK contained uncommitted changes at
capture time; this is a bundled snapshot, not an identified upstream release.

Source project: https://github.com/Sasye/ApplepieManager
License: [GNU AGPL v3](LICENSE).

The five files below include every SDK header needed to compile SynchroFocus:

- `applepie.h`: public C ABI and service tables.
- `applepie_client.h`: header-only C++ client and service helpers.
- `applepie_native.h`: native Hook helpers.
- `applepie_seh.h`: SEH call helpers.
- `applepie_pointer_scope.h`: scoped pointer-patch helpers.

`build.bat` always uses this directory and checks that all five headers exist.
It does not use `APPLEPIE_SDK_DIR`, download dependencies, or require the Manager
repository, a prebuilt Manager DLL, or an Applepie import library. The remaining
build dependencies are the MSVC x64 C++17 toolchain and the Windows SDK.

At runtime, a compatible Applepie Manager supplies the services through
`APH_Attach`. This snapshot does not include the Manager/Core implementation,
a proxy loader, or a private Hook backend.
