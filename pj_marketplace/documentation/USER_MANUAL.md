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
| **Version** | Current version |
| **Status** | `[install]`, `[installed]`, or `[update]` |

**To see extension details:** Double-click on any extension to open a detail dialog with full information (description, author, changelog).

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

**Quick filters:**
- `@installed` — Show only installed extensions
- `@updates` — Show extensions with available updates

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
5. If something goes wrong, the old version is restored

**Update All:** Click "Update All" in the toolbar to update all extensions at once

### 2.6 Uninstalling an Extension

1. Click on the installed extension
2. Click **Uninstall**
3. Confirm in the dialog
4. Extension files are removed

### 2.7 Enabling/Disabling Extensions

You can disable an extension without uninstalling:
1. Click on the installed extension
2. Click **Disable**
3. Extension remains installed but won't load

To re-enable:
1. Click on the disabled extension
2. Click **Enable**

---

## 3. Developer Guide

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
   - Update `manifest.json.in` with your extension info
   - Add UI in `ui/my_dialog.ui` (optional)

3. **Build locally:**
   ```bash
   conan install . --profile profiles/linux_static --build=missing
   cmake --preset conan-release
   cmake --build --preset conan-release
   ```

4. **Test locally:**
   - Copy built files to `~/.plotjuggler/extensions/my-extension/`
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

### 3.2 Extension Manifest

Every extension needs a `manifest.json`:

```json
{
  "id": "my-extension",
  "version": "1.0.0",
  "min_plotjuggler_version": "4.0.0",
  "plugins": [
    {
      "name": "MyPlugin",
      "type": "data_loader",
      "library": "libmy_plugin",
      "ui_file": "my_dialog.ui"
    }
  ]
}
```

| Field | Required | Description |
|-------|----------|-------------|
| `id` | Yes | Unique identifier (lowercase, hyphens) |
| `version` | Yes | Semantic version (X.Y.Z) |
| `min_plotjuggler_version` | Yes | Minimum compatible PJ version |
| `plugins` | Yes | Array of plugins in this extension |
| `plugins[].name` | Yes | C++ class name |
| `plugins[].type` | Yes | data_loader, data_streamer, parser, toolbox |
| `plugins[].library` | Yes | Library name without extension |
| `plugins[].ui_file` | No | Qt Designer .ui file |

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

### 4.1 Common Issues

| Problem | Cause | Solution |
|---------|-------|----------|
| "Extension not loading" | Incompatible version | Check `min_plotjuggler_version` |
| "Download failed" | Network issue | Check internet, try again |
| "Checksum mismatch" | Corrupted download | Try again, report if persistent |
| "Cannot update (Windows)" | DLL in use | Restart PlotJuggler |
| "Extension disappeared" | Rollback occurred | Check logs, previous version restored |

### 4.2 Log Locations

| OS | Path |
|----|------|
| Linux | `~/.local/share/PlotJuggler/logs/` |
| Windows | `%APPDATA%/PlotJuggler/logs/` |
| macOS | `~/Library/Application Support/PlotJuggler/logs/` |

### 4.3 Reset Marketplace

If the marketplace is broken:

```bash
# Linux/macOS
rm -rf ~/.plotjuggler/extensions/
rm ~/.plotjuggler/installed.json
rm -rf ~/.plotjuggler/.cache/

# Windows
rmdir /s %USERPROFILE%\.plotjuggler\extensions
del %USERPROFILE%\.plotjuggler\installed.json
rmdir /s %USERPROFILE%\.plotjuggler\.cache
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

```
~/.plotjuggler/
├── extensions/           # Installed extensions
│   └── my-extension/
│       ├── manifest.json
│       └── libmy_plugin.so
├── .pending/            # Staged updates (Windows)
├── .backup/             # Backup of previous versions
├── .cache/              # Registry cache
│   └── registry.json
└── installed.json       # Local state
```

### 5.2 Registry URL

**Default:** `https://raw.githubusercontent.com/plotjuggler/marketplace-registry/main/registry.json`

**Custom registry:** Set in PlotJuggler settings or environment variable:
```bash
export PLOTJUGGLER_REGISTRY_URL=https://your-company.com/registry.json
```

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
| `PLAN.md` | Current work plan and TODOs |

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
| Extension list | `src/ui/ExtensionListWidget.cpp` |
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
