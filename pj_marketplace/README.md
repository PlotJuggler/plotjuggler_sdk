# pj_marketplace

Extension marketplace for PlotJuggler — handles registry fetching, package downloads, and extension lifecycle management.

## Dependencies

All dependencies are managed by Conan:

- **Qt6** (Core, Widgets, Network, Test)
- **libarchive**
- **GTest**

## Build

```bash
# Install Conan if not available
pip install conan

# Detect Conan profile (first time only)
conan profile detect

# Build
./build.sh
```

Build output:
- `build/libpj_marketplace.a` — static library
- `build/pj_marketplace_app` — standalone executable
- `build/tests/` — test executables

## Build Options

```bash
./build.sh           # RelWithDebInfo (default)
./build.sh --debug   # Debug build with ASAN
```

## Run Tests

```bash
cd build && ctest --output-on-failure
```

## Project Structure

```
pj_marketplace/
├── src/
│   ├── core/
│   │   ├── DownloadManager.cpp/.h    # HTTP download + checksum + libarchive extraction
│   │   ├── ExtensionManager.cpp/.h   # Install/uninstall/update lifecycle
│   │   ├── PlatformUtils.cpp/.h      # Cross-platform paths and detection
│   │   └── RegistryManager.cpp/.h    # Remote registry fetching
│   └── models/
│       ├── Extension.h               # Extension metadata
│       ├── InstalledExtension.h      # Installed extension record
│       └── Platform.h                # Platform-specific artifact
├── tests/
│   ├── download_manager_test.cpp
│   ├── extension_manager_test.cpp
│   └── registry_manager_test.cpp
├── build.sh                          # Standalone build script
├── conanfile.txt                     # Conan dependencies
└── CMakeLists.txt
```
