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

class RawImageParser final : public pj::FunctionalParser {
 public:
  pj::Status bind(const pj::BindingInfo& info) override {
    return info.route() == pj::Route::kObject
               ? pj::Status::ok()
               : pj::Status::decline("object route only");
  }

  pj::Status parseObject(pj::PayloadView payload, pj::Timestamp timestamp,
                         pj::ObjectWriter& output) override {
    auto image = output.image();
    if (timestamp.has_value) {
      if (auto status = image.setTimestamp(timestamp.nanoseconds);
          !status.isOk()) {
        return status;
      }
    }
    if (auto status = image.setEncoding("mono8"); !status.isOk()) {
      return status;
    }
    return image.setData(payload);
  }
};

PJ_FUNCTIONAL_PARSER(RawImageParser)
```

Override `parseScalars(PayloadView, Timestamp, ScalarWriter&)` for scalar
claims. `ObjectWriter` provides `image`, `pointCloud`, `depthImage`,
`occupancyGrid`, `compressedPointCloud`, `mesh3D`, `videoFrame`,
`occupancyGridUpdate`, and `voxelGrid` builders.

Native modules are built with `pj_add_parser_module(... TARGETS native)`. The
target links no SDK library; it receives this subtree only as an include path.
`TARGETS wasm` is not available in SDK 0.22. The wasi-sdk gate compiles and
statically audits a reactor fixture, but it does not provide wasm authoring or
execution.

See
`.claude/skills/plotjuggler-plugin/references/parser-module.md` at the repository
root for the manifest, schema-locator, splice, lifetime, and error contracts.
