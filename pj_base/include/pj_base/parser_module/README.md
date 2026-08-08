# Functional parser-module authoring kit

This directory is a standalone, header-only C++17 API. Headers in this subtree
may include only other headers from this subtree and C/C++ standard-library
headers. They must remain suitable for a WASI reactor build: no filesystem,
threads, iostreams, host SDK linkage, or exceptions crossing public/ABI calls.

All data received from a host or message is borrowed through `ByteView` /
`PayloadView`. Fallible operations return the local `pj::Status` or
`pj::Expected<T>` types. Output storage is owned by `pj::Blob`, whose allocation
entry points report failure instead of exposing allocation exceptions.

The umbrella include for module authors is:

```cpp
#include <pj_base/parser_module/module.hpp>
```

Native modules are built with `pj_add_parser_module`. The target links no SDK
library; it receives this subtree only as an include path.

WASI reactor modules use the same operational exports and compile with
exceptions disabled. Their manifest is delivered in the
`pj_parser_module_manifest` custom section, so the native-only manifest address
and length exports are omitted automatically when targeting wasm.
