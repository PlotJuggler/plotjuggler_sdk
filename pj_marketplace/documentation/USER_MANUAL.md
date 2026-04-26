# PlotJuggler Marketplace — User Manual

> **Version:** 1.0.0
> **Last Updated:** 2026-03-04
> **Audience:** End users, developers, and LLMs assisting with the project

---

## 1. Quick Start

### For End Users

1. Open PlotJuggler
2. Go to **Plugins → Open Marketplace**
3. Search for the extension you need (e.g., "ROS 2")
4. Click **Install**
5. Restart PlotJuggler if prompted
6. Your new plugin is ready to use

### For Plugin Developers

1. Use the [extension-template](https://github.com/plotjuggler/extension-template) on GitHub
2. Click "Use this template" to create your repo
3. Modify the plugin code in `src/`
4. Push a tag (`git tag v1.0.0 && git push --tags`)
5. CI automatically builds, packages, and publishes
6. Submit PR to add your extension to the registry

---

## 2. User Guide

### 2.1 Opening the Marketplace

**From PlotJuggler:**
- Menu: `Plugins → Open Marketplace`
- Keyboard shortcut: (TBD)

**Standalone (development only):**
```bash
./pj_marketplace
```

### 2.2 Browsing Extensions

The marketplace window shows a list of all extensions with their status:

| Column | Content |
|--------|---------|
| **Name** | Extension name |
| **Version** | Installed version when available, otherwise registry version; registry version is shown for comparison when they differ |
| **Status** | `[install]`, `[installed]`, `[update]`, or `[local newer]` |

**To see extension details:** Double-click on any extension to open a detail dialog with full information (description, author, changelog).

If PlotJuggler already has the plugin loaded at startup, the marketplace is seeded with that loaded state before the first render so the card version matches what the app already opened.

**Quick tip:** Hover over an extension to see a brief description tooltip.

### 2.3 Searching and Filtering

**Search box:** Type to filter by name, description, or tags
- Example: `ros` finds "ROS 2 Streaming", "ROS Bag Loader"
- Example: `csv` finds "CSV Loader", "CSV Exporter"

**Category filter dropdown:**
- All
- Data Loader
- Data Streamer
- Parser
- Toolbox

**Quick filters:** *(planned — not yet implemented)*

### 2.4 Installing an Extension

1. Find the extension in the list
2. (Optional) Double-click to see details in a dialog
3. Click the **[install]** button next to the extension
4. Wait for download and extraction
5. See "Installation complete" message
6. Button changes to **[installed]**

**On Windows:** You may see "Restart required to complete installation"

### 2.5 Updating an Extension

1. Extensions with updates show an **Update available** badge
2. Click on the extension
3. Click **Update**
4. The old version is automatically backed up
5. If something goes wrong, the old version remains in `.backup/` and can be recovered manually

**Update All:** Click "Update All" in the toolbar to update all extensions at once

### 2.6 Uninstalling an Extension

1. Click on the installed extension
2. Click **Uninstall**
3. Confirm in the dialog
4. Extension files are removed

### 2.7 Enabling/Disabling Extensions

*Planned — see [TODO.md](TODO.md). Today, removing an extension requires Uninstall.*

---

## 3. Developer Guide

> **Note (POC vs Real Plugins):**
>
> During the POC phase (March 2026), we use **dummy plugins** that only have a `getMetadata()` function — no Qt, no SDK dependency. This simplifies CI and cross-platform builds.
>
> **Real plugins** (post-POC) will use the PlotJuggler SDK and may have Qt UI files. The workflow below describes the full process for real plugins.
>
> **CI Options:** Extensions can live in separate repos (one per extension) or in a mono-repo with per-component releases (see [Foxglove MCAP](https://github.com/foxglove/mcap) as reference). The registry supports both approaches.

### 3.1 Creating a New Extension

**Prerequisites:**
- C++ development environment
- CMake 3.16+
- Conan package manager
- Git

**Steps:**

1. **Create repo from template:**
   ```bash
   # Go to https://github.com/plotjuggler/extension-template
   # Click "Use this template"
   # Clone your new repo
   git clone https://github.com/YOUR_USERNAME/my-extension.git
   cd my-extension
   ```

2. **Modify the plugin:**
   - Edit `src/my_plugin.cpp`
   - Update the embedded manifest string passed to the SDK export macro
   - Add UI in `ui/my_dialog.ui` (optional)

3. **Build locally:**
   ```bash
   conan install . --profile profiles/linux_static --build=missing
   cmake --preset conan-release
   cmake --build --preset conan-release
   ```

4. **Test locally:**
   - Copy built files to your platform's extensions directory (Linux: `~/.local/share/plotjuggler/extensions/my-extension/` — see §5.1 for other OSes)
   - Open PlotJuggler and verify plugin loads

5. **Release:**
   ```bash
   git add .
   git commit -m "feat: my awesome plugin"
   git tag v1.0.0
   git push && git push --tags
   ```

6. **Wait for CI:**
   - CI builds for Linux, Windows, macOS
   - CI creates GitHub Release with artifacts
   - CI generates registry PR

7. **Submit to registry:**
   - Review the auto-generated PR
   - Merge to add to public marketplace

### 3.2 Embedded Plugin Manifest

Every plugin DSO must export an embedded manifest through the SDK macro:

```cpp
PJ_DATA_SOURCE_PLUGIN(MyPlugin,
    R"({"id":"my-extension","name":"My Extension","version":"1.0.0"})")
```

| Field | Required | Description |
|-------|----------|-------------|
| `id` | Yes | Unique identifier (lowercase, hyphens) |
| `name` | Yes | Human-readable plugin name |
| `version` | Yes | Semantic version (X.Y.Z) |
| `encoding` | Parsers only | Message encoding handled by a parser plugin |
| `file_extensions` | No | File suffixes handled by a file source plugin |
| `capabilities` | No | Optional capability tags |

### 3.3 Plugin Types

| Type | Interface | Purpose |
|------|-----------|---------|
| `data_loader` | `PJ::DataLoader` | Load data from files |
| `data_streamer` | `PJ::DataStreamer` | Real-time data streaming |
| `parser` | `PJ::MessageParser` | Parse binary data to fields |
| `toolbox` | `PJ::ToolboxPlugin` | Custom tools with UI |

### 3.4 Best Practices

- **No Qt dependency:** Use the SDK, not Qt directly
- **Static linking:** Embed all dependencies
- **Test on all platforms:** Use CI matrix build
- **Semantic versioning:** Follow semver strictly
- **Clear README:** Explain what your plugin does
- **License:** Include LICENSE file (Apache-2.0 recommended)

---

## 4. Troubleshooting

### 4.0 Diagnostics dialog

The marketplace toolbar shows a **Details** button whenever there are recent diagnostics. Click it to open a read-only log of marketplace lifecycle events (install/uninstall/staged-promotion failures, quarantine moves, registry-fetch failures). The most recent error also appears in the status bar; a successful registry refresh clears the sticky error so progress messages aren't suppressed.

When the marketplace runs **inside** a host application (e.g. PlotJuggler), the host can subscribe to the same diagnostic stream alongside its own plugin-load events; see ARCHITECTURE.md §3.3 *ExtensionManager — Diagnostic propagation*.

### 4.1 Common Issues

| Problem | Cause | Solution |
|---------|-------|----------|
| "Extension not loading" | Incompatible version | Check `min_plotjuggler_version` |
| "Download failed" | Network issue | Check internet, try again |
| "Checksum mismatch" | Corrupted download | Try again, report if persistent |
| "Cannot update (Windows)" | DLL in use | Restart PlotJuggler |
| "Installed version is newer" | Local plugin is ahead of registry | Downgrade is blocked; keep the local version |
| "Update failed after backup" | New artifact did not install | Check marketplace diagnostics for the retained backup path |
| "Post-promotion validation failed" | The DSO loads in the staging area but not from `extensions/` (rpath/dep issue) | The install is rolled back; check the diagnostic for the linker error |
| "Could not mark … for restart cleanup" | Marketplace could not write the `.pj_pending_uninstall` marker (Windows; permissions or AV) | The uninstall is **not** scheduled; resolve the file-permission issue and retry |
| "Moved to quarantine: …" | A previous staged update could not be removed; it has been moved aside | Inspect the quarantined directory and delete it manually once safe |
| "Invalid registry URL" | The Settings dialog rejected a malformed URL | Use a `http://`, `https://`, or `file://` URL |

### 4.2 Log Locations

| OS | Path |
|----|------|
| Linux | `~/.local/share/PlotJuggler/logs/` |
| Windows | `%APPDATA%/PlotJuggler/logs/` |
| macOS | `~/Library/Application Support/PlotJuggler/logs/` |

### 4.3 Reset Marketplace

If the marketplace is broken, remove the extensions and staging directories under your platform's config root (see §5.1):

```bash
# Linux
rm -rf ~/.local/share/plotjuggler/extensions/
rm -rf ~/.local/share/plotjuggler/.extension_staging/

# macOS
rm -rf ~/Library/Application\ Support/plotjuggler/extensions/

# Windows
rmdir /s %LOCALAPPDATA%\plotjuggler\extensions
rmdir /s %LOCALAPPDATA%\plotjuggler\.extension_staging
```

### 4.4 Reporting Bugs

1. Check existing issues at https://github.com/plotjuggler/marketplace/issues
2. Include:
   - PlotJuggler version
   - OS and version
   - Extension name and version
   - Error message or log excerpt
   - Steps to reproduce

---

## 5. Reference

### 5.1 Directory Structure

The marketplace uses the OS-standard writable data location (resolved by `QStandardPaths::GenericDataLocation`):

| OS | Root |
|----|------|
| Linux | `~/.local/share/plotjuggler/` |
| macOS | `~/Library/Application Support/plotjuggler/` |
| Windows | `%LOCALAPPDATA%/plotjuggler/` |

Inside that root:

```
<config-root>/
├── extensions/                      # Active installed extensions
│   └── my-extension/
│       └── libmy_plugin.so
├── .extension_staging/      # Staging area (all platforms — Windows uses it
│   │                                # for restart-time installs; Linux/macOS
│   │                                # use it as the post-promotion validation gate)
│   └── my-extension/
│       └── .pj_pending_install      # Intent file (Windows-only restart-time apply)
└── .backup/                         # Pre-update backups (all platforms); automatic rollback deferred — restore manually
```

### 5.2 Registry URL

**Default:** `https://raw.githubusercontent.com/plotjuggler/marketplace-registry/main/registry.json`

**Custom registry:** Open the marketplace, click ⚙ Settings, paste the new URL, click OK. The URL is persisted under `QSettings("PlotJuggler", "Marketplace")/registry_url` and restored on next launch.

### 5.3 Supported Platforms

| Platform | Architecture | Status |
|----------|--------------|--------|
| Linux | x86_64 | Full support |
| Linux | arm64 | Planned |
| Windows | x86_64 | Full support |
| macOS | arm64 (Apple Silicon) | Full support |
| macOS | x86_64 (Intel) | Planned |

### 5.4 Extension Categories

| Category | Code | Examples |
|----------|------|----------|
| Data Loader | `data_loader` | CSV, MCAP, ROS bags |
| Data Streamer | `data_streamer` | ROS 2, MQTT, ZMQ |
| Parser | `parser` | Protobuf, FlatBuffers |
| Toolbox | `toolbox` | FFT, CSV exporter |
| Bundle | `bundle` | ROS 2 Complete |

---

## 6. For LLMs/AI Assistants

### 6.1 Project Context

This is the **PlotJuggler Marketplace**, an extension distribution system for PlotJuggler (a robotics data visualization tool). Key points:

- **Stack:** C++17, Qt 6 Widgets, CMake, Conan
- **Architecture:** Serverless (GitHub-hosted registry and artifacts)
- **Key innovation:** Plugins don't depend on Qt (ABI stability)

### 6.2 Key Files

| File | Purpose |
|------|---------|
| `REQUIREMENTS.md` | What the system should do |
| `ARCHITECTURE.md` | How the system is designed |
| `USER_MANUAL.md` | This file - how to use it |
| `TODO.md` | Remaining work and deferred follow-ups |

### 6.3 Common Tasks

**"Add a new feature"**
1. Check if it's in REQUIREMENTS.md
2. Design in ARCHITECTURE.md
3. Implement following the code structure
4. Update USER_MANUAL.md if user-facing

**"Fix a bug"**
1. Reproduce the issue
2. Find relevant component in ARCHITECTURE.md
3. Fix and test
4. Update docs if behavior changed

**"Help user install extension"**
1. Guide through Section 2.4 of this manual
2. Check troubleshooting if issues arise

### 6.4 Code Locations

| Component | Path |
|-----------|------|
| Registry fetching | `src/core/RegistryManager.cpp` |
| Installation logic | `src/core/ExtensionManager.cpp` |
| Download handling | `src/core/DownloadManager.cpp` |
| Main UI | `src/ui/MarketplaceWindow.cpp` |
| Extension detail dialog | `src/ui/extension_detail_dialog.cpp` |
| Data models | `src/models/` |

### 6.5 Testing

```bash
# Build
cmake --preset conan-release
cmake --build --preset conan-release

# Run tests
ctest --preset conan-release

# Run standalone
./build/release/pj_marketplace
```

---

## Document Maintenance

Update this manual when:
- User-facing features change
- New troubleshooting items discovered
- Developer workflow changes
- New platforms supported
