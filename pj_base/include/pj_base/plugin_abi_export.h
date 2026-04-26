#ifndef PJ_PLUGIN_ABI_EXPORT_H
#define PJ_PLUGIN_ABI_EXPORT_H

#include "pj_base/plugin_data_api.h"

// Boot-level ABI symbol the host loader checks before touching any vtable.
//
// Defined at file scope (not inside a macro) so a single TU can use multiple
// PJ_*_PLUGIN(...) macros — e.g. DataSource + Dialog co-resident — without
// producing a same-TU duplicate-symbol error. The header is `#pragma once`,
// so any TU that pulls it in (directly or transitively via a family base
// header) gets exactly one definition.
//
// Linkage attribute lets multiple TUs in the same DSO each emit a definition
// and have the linker fold them into a single COMDAT entry. `used` forces
// emission even though no in-TU code references the symbol — the host reads
// it via dlsym, which the compiler can't see.
//
// Note: the variable is intentionally non-const. A namespace-scope `const`
// variable has internal linkage by default in C++, and MSVC then rejects
// `__declspec(selectany)` on it with error C2496 ("can only be applied to
// data items with external linkage"). `extern "C"` controls language linkage
// (name mangling), not internal/external linkage. Dropping `const` gives the
// variable external linkage on every toolchain so the COMDAT/weak fold is
// well-defined; nothing inside any plugin DSO writes to the symbol — the host
// only reads it once via dlsym during ABI handshake.
//
// The block form `extern "C" { ... }` (rather than the single-decl form
// `extern "C" T x = val;`) is required so that GCC does not treat the
// declaration as `extern` with initializer and trip -Wextern-initializer.
#if defined(_MSC_VER)
#define PJ_PLUGIN_ABI_LINK __declspec(dllexport) __declspec(selectany)
#else
#define PJ_PLUGIN_ABI_LINK \
  __attribute__((visibility("default"))) __attribute__((weak)) __attribute__((used))
#endif

extern "C" {
PJ_PLUGIN_ABI_LINK uint32_t pj_plugin_abi_version = PJ_ABI_VERSION;
}

// No-op marker. The actual `pj_plugin_abi_version` definition lives at file
// scope above; this macro exists so each `PJ_*_PLUGIN(...)` family macro can
// document, in-line at its own expansion site, that an ABI-version export is
// part of the contract. The `EXPORT_TAG` argument is intentionally ignored.
#define PJ_EXPORT_PLUGIN_ABI_VERSION(EXPORT_TAG) /* emitted at file scope */

#endif  // PJ_PLUGIN_ABI_EXPORT_H
