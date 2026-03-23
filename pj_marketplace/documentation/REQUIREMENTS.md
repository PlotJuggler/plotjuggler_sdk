# PlotJuggler Marketplace — Requirements

> **Version:** 1.0.0
> **Last Updated:** 2026-03-16
> **Purpose:** Define WHAT the application should do, not HOW

---

## 1. Problem Statement

PlotJuggler has grown significantly, evolving from an internal tool to a de facto standard for data visualization in robotics. With this growth comes a problem: **how do we allow the community to contribute plugins without requiring a full PlotJuggler recompilation for each update?**

### Current Pain Points

1. Users must recompile plugins when PlotJuggler updates
2. Qt version mismatches break plugins silently
3. High barrier to entry for plugin developers (CMake, Conan, CI setup)
4. No central discovery mechanism for available plugins
5. Manual installation process is error-prone

---

## 2. Feature Overview

### 2.1 Client Features (Marketplace UI)

| Category           | Feature              | Description                                                   |
| ------------------ | -------------------- | ------------------------------------------------------------- |
| **Discovery**      | Extension listing    | Display all available extensions in VSCode-style cards        |
|                    | Search               | Search by name, description, tags, and publisher              |
|                    | Category filtering   | Data Loader, Data Streamer, Parser, Toolbox                   |
|                    | Extension detail     | Panel with complete information, changelog, and dependencies  |
| **Installation**   | Secure download      | ZIP artifact download with SHA256 verification                |
|                    | Automatic extraction | Decompression to extensions directory                         |
|                    | Platform detection   | Automatic selection of correct artifact (Linux/Windows/macOS) |
| **Updates**        | Update detection     | Local vs registry version comparison (semver)                 |
|                    | Individual update    | Update a specific extension                                   |
|                    | Bulk update          | "Update All" for multiple extensions                          |
|                    | Automatic backup     | Backup of previous version before updating                    |
| **Uninstallation** | Clean removal        | Directory deletion + local state update                       |
|                    | Confirmation         | Confirmation dialog before uninstalling                       |
| **Management**     | Enable/Disable       | Activate/deactivate extensions without uninstalling           |
|                    | Rollback             | Automatic restoration if a plugin fails to load               |
|                    | Persistent state     | Installed state derived from disk — each extension's manifest.json is the source of truth |
|                    | Registry URL settings | Configure registry URL at runtime via ⚙ settings dialog; change triggers immediate refresh |
|                    | Registry URL persistence | Last configured registry URL saved and restored between sessions |
| **UI/UX**          | Download progress    | Progress bar in status bar                                    |
|                    | Notifications        | Status messages and available update alerts                   |
|                    | Context menu         | Quick actions per installed extension                         |

### 2.2 CI System Features (For Developers)

| Category       | Feature                    | Description                                                |
| -------------- | -------------------------- | ---------------------------------------------------------- |
| **Build**      | Cross-platform compilation | Matrix build for Linux, Windows, and macOS                 |
|                | Static linking             | All dependencies embedded in the artifact                  |
|                | Dependency management      | Support for Conan (current) and Pixi (future)              |
| **Packaging**  | ZIP generation             | Automatic packaging with manifest, binaries, and resources |
|                | Checksums                  | Automatic SHA256 generation per artifact                   |
|                | Versioning                 | Version extraction from git tag                            |
| **Publishing** | GitHub Release             | Automatic release creation with attached artifacts         |
|                | Registry update            | Automatic PR to registry repo with new version             |
| **Validation** | Unit tests                 | Test execution on each platform                            |
|                | Lint/Format                | Code style verification                                    |
|                | Schema validation          | Registry JSON validation in PRs                            |

### 2.3 Registry Features

| Category    | Feature             | Description                                           |
| ----------- | ------------------- | ----------------------------------------------------- |
| **Catalog** | Complete listing    | JSON with all available extensions                    |
|             | Metadata            | Name, description, author, license, tags, category    |
|             | Versioning          | Current version and minimum PlotJuggler versions      |
|             | Cross-platform      | URLs and checksums per platform (Linux/Windows/macOS) |
| **Hosting** | Static GitHub       | JSON file accessible via raw.githubusercontent.com    |
|             | Cache TTL           | Support for local cache with expiration time          |
|             | Multiple registries | Configuration for alternative registries (enterprise) |

---

## 3. Terminology

| Term | Definition |
|------|------------|
| **Extension** | Marketplace distribution unit. Downloadable ZIP containing one or more plugins. |
| **Plugin** | C++ module dynamically loaded (.so/.dll/.dylib) implementing an SDK interface. |
| **Registry** | Static JSON file with the catalog of available extensions. |
| **Plugin SDK** | Abstract library (no Qt) that plugins use for UI and data access. |
| **Artifact** | Compiled binary of an extension for a specific platform. |
| **Manifest** | JSON file inside the ZIP describing the extension contents. |

---

## 4. Functional Requirements

### 4.1 P0 — Minimum Viable Product

| ID | Requirement | Acceptance Criteria |
|----|-------------|---------------------|
| F-01 | Fetch and parse registry JSON from configurable URL | Given a valid URL, the system loads and parses extension metadata |
| F-02 | List extensions in sidebar with cards | User sees all available extensions with name, description, version |
| F-03 | Search by name, description, tags | Typing "ros" shows all ROS-related extensions |
| F-04 | Filter by category | User can filter by Data Loader, Streamer, Parser, Toolbox |
| F-05 | Show selected extension detail | Clicking an extension shows full information panel |
| F-06 | Download ZIP with SHA256 verification | Download fails if checksum doesn't match |
| F-07 | Extract ZIP to extensions directory | ZIP contents are extracted to correct location |
| F-08 | Register installed extension | Installed state is derived from disk by scanning extensions_dir and reading manifest.json from each subdirectory |
| F-09 | Detect updates (local vs registry version) | User sees "Update available" badge when newer version exists |
| F-10 | Uninstall extension | User can remove installed extensions |

### 4.2 P1 — Robustness

| ID | Requirement | Acceptance Criteria |
|----|-------------|---------------------|
| F-24 | Configure registry URL via settings dialog | User can open ⚙ settings, enter a custom URL, and the marketplace immediately fetches from the new URL |
| F-25 | Persist registry URL between sessions | The last configured registry URL is saved and automatically restored on next launch |
| F-11 | Local registry cache with TTL | Registry is cached locally, refreshed after expiration |
| F-12 | Backup previous version on updates | Old version saved before overwriting |
| F-13 | Automatic rollback if plugin fails | If plugin crashes on load, previous version is restored |
| F-14 | Windows staging: apply on restart | Updates downloaded but applied only after restart (Windows) |
| F-15 | Enable/Disable without uninstalling | User can deactivate extension without removing files |
| F-16 | Cancel download in progress | User can abort a download |
| F-17 | Update All | Single action to update all extensions with available updates |
| F-18 | Confirmation dialogs | User confirms before install/uninstall/update actions |

### 4.3 P2 — Polish

| ID | Requirement | Acceptance Criteria |
|----|-------------|---------------------|
| F-19 | Extension icons (download + cache) | Each extension displays its icon |
| F-20 | Changelog per extension | User can see version history |
| F-21 | Metrics (downloads, rating) | Extension cards show popularity metrics |
| F-22 | Notification: "N updates available" | User notified of available updates |
| F-23 | Multiple registry URLs | Support for private/enterprise registries |

---

## 5. Non-Functional Requirements

| ID | Requirement | Metric |
|----|-------------|--------|
| NF-01 | C++17 minimum | Code compiles with C++17 standard |
| NF-02 | Qt 6.x Widgets | LTS 6.8 target |
| NF-03 | Cross-platform | Works on Linux, Windows, macOS |
| NF-04 | Build system: CMake | Standard CMake project |
| NF-05 | Dependencies: Conan (current), Pixi (future) | Builds with specified tools |
| NF-06 | No external dependencies beyond Qt | Single binary, no runtime deps |
| NF-07 | Standalone → integrable into PlotJuggler | Works as standalone, then embeds |
| NF-08 | Download in background thread | UI remains responsive during downloads |
| NF-09 | Performance: <100ms to load/filter registry | With ~50 extensions |
| NF-10 | Static linking in extensions | Plugins are self-contained |

---

## 6. Use Cases

### UC-01: User Discovers and Installs Extension

**Actor:** PlotJuggler User
**Preconditions:** PlotJuggler is running, internet available
**Flow:**
1. User opens Marketplace (Plugins → Marketplace)
2. User searches for "ROS 2"
3. System shows matching extensions
4. User clicks on "ROS 2 Streaming"
5. User sees extension details (description, version, author)
6. User clicks "Install"
7. System downloads ZIP, verifies checksum, extracts
8. System shows "Installation complete"
9. User closes Marketplace
10. Plugin is available in PlotJuggler

**Postconditions:** Extension installed and registered

### UC-02: User Updates Extension

**Actor:** PlotJuggler User
**Preconditions:** Extension installed, newer version available
**Flow:**
1. User opens Marketplace
2. User sees "Update available" badge on installed extension
3. User clicks "Update"
4. System backs up current version
5. System downloads and installs new version
6. System shows "Update complete"

**Postconditions:** New version installed, old version backed up

### UC-03: User Uninstalls Extension

**Actor:** PlotJuggler User
**Preconditions:** Extension installed
**Flow:**
1. User opens Marketplace
2. User navigates to installed extensions
3. User clicks "Uninstall" on extension
4. System shows confirmation dialog
5. User confirms
6. System removes extension files
7. System updates local state

**Postconditions:** Extension removed, local state updated

### UC-04: Plugin Fails to Load (Rollback)

**Actor:** System
**Preconditions:** Extension recently updated, backup exists
**Flow:**
1. PlotJuggler starts
2. System attempts to load plugin
3. Plugin crashes/fails
4. System detects failure
5. System restores backup version
6. System notifies user of rollback

**Postconditions:** Previous version restored, user notified

### UC-05: Developer Publishes Extension

**Actor:** Plugin Developer
**Preconditions:** Developer has GitHub account, uses template
**Flow:**
1. Developer creates tag (v1.0.0)
2. CI compiles for all platforms
3. CI packages ZIPs with manifest
4. CI creates GitHub Release
5. CI submits PR to registry repository
6. Registry validates schema and URLs
7. PR is merged
8. Extension appears in marketplace

**Postconditions:** Extension available to all users

---

## 7. Extension Categories

| Category | Value | Description | Example |
|----------|-------|-------------|---------|
| Data Loader | `data_loader` | Loads data from files (atomic operation) | CSV, MCAP, ROS bags |
| Data Streamer | `data_streamer` | Continuous streaming at 50Hz, thread-safe | ROS 2, MQTT, ZMQ |
| Parser | `parser` | Conversion from byte blob to individual fields | Protobuf, FlatBuffers |
| Toolbox | `toolbox` | Tools with GUI (FFT, CSV export, quaternion) | FFT analyzer |
| Bundle | `bundle` | ZIP with multiple plugins from different families | ROS 2 Complete |

---

## 8. Corner Cases and Edge Cases

### 8.1 Network Issues

| Scenario | Expected Behavior |
|----------|-------------------|
| No internet connection | Show cached registry (if available), disable install/update |
| Registry URL unreachable | Show error message, offer retry |
| Download interrupted | Partial file deleted, user can retry |
| Checksum mismatch | Download rejected, user notified |

### 8.2 File System Issues

| Scenario | Expected Behavior |
|----------|-------------------|
| Insufficient disk space | Error before extraction, suggest cleanup |
| No write permission to extensions dir | Clear error message, suggest running with permissions |
| Extension directory doesn't exist | Create it automatically |
| Corrupted ZIP file | Extraction fails gracefully, user notified |

### 8.3 Version Conflicts

| Scenario | Expected Behavior |
|----------|-------------------|
| Extension requires newer PlotJuggler | Show warning, prevent install |
| Downgrade requested | Allow with warning |
| Same version reinstall | Ask confirmation, then reinstall |

### 8.4 Windows-Specific

| Scenario | Expected Behavior |
|----------|-------------------|
| Plugin DLL in use (can't overwrite) | Stage update, apply on restart |
| User cancels pending update | Remove staged files |
| PlotJuggler crashes before applying update | Pending update remains for next start |

### 8.5 Plugin Loading

| Scenario | Expected Behavior |
|----------|-------------------|
| Plugin crashes on load | Rollback to backup if exists, else disable |
| Plugin incompatible with current SDK | Clear error message, don't load |
| Manifest missing or invalid | Extension marked as corrupted |

---

## 9. Constraints

### 9.1 Must NOT Do

- **No backend server** — All hosting via GitHub (serverless)
- **No Qt dependency in plugins** — Plugins use abstract SDK only
- **No database** — JSON files for registry and local state
- **No user accounts** — Anonymous usage
- **No telemetry** — No data collection without consent

### 9.2 Assumptions

- Users have internet access for initial install
- GitHub raw URLs remain accessible
- Extensions are < 50MB compressed
- Registry has < 100 extensions in foreseeable future
- **POC phase:** Dummy plugins are pure C++ with no Qt dependency (only `getMetadata()` function), simplifying cross-platform CI

### 9.3 Out of Scope (v1.0)

- Paid extensions / license management
- Dependency resolution between extensions
- Automatic updates (always user-initiated)
- Plugin sandboxing / security isolation
- Extension ratings / reviews (metrics only)

---

## 10. Acceptance Criteria (MVP)

The minimum viable product is successful if:

1. [ ] Opens as standalone Qt Widgets app
2. [ ] Loads registry JSON from URL (GitHub raw)
3. [ ] Shows extension list with cards
4. [ ] Allows searching and filtering by category
5. [ ] Shows selected extension detail
6. [ ] Downloads ZIP with checksum verification
7. [ ] Extracts to local directory and registers as installed
8. [ ] Detects new available versions
9. [ ] Allows extension uninstallation
10. [ ] Works on Linux (Windows/macOS as stretch goal for MVP)

---

## 11. Data Formats

### 11.1 Registry JSON Schema

```json
{
  "registry_version": "1.0",
  "last_updated": "ISO8601 timestamp",
  "extensions": [
    {
      "id": "unique-extension-id",
      "name": "Display Name",
      "description": "Short description",
      "author": "Author Name",
      "publisher": "Publisher Name",
      "website": "https://...",
      "repository": "https://github.com/...",
      "license": "SPDX identifier",
      "icon_url": "https://... (optional)",
      "category": "data_loader|data_streamer|parser|toolbox|bundle",
      "tags": ["tag1", "tag2"],
      "version": "semver",
      "min_plotjuggler_version": "semver",
      "plugins": [
        {
          "name": "PluginClassName",
          "type": "plugin_type",
          "library": "library_name_without_extension"
        }
      ],
      "platforms": {
        "linux-x86_64": {
          "url": "https://...",
          "checksum": "sha256:..."
        }
      },
      "changelog": {
        "1.0.0": "Initial release"
      }
    }
  ]
}
```

### 11.2 Installed State

There is no separate local state file. Installed extensions are discovered at runtime by
scanning `extensions_dir` and reading the `manifest.json` present in each subdirectory.
The `manifest.json` is part of the artifact ZIP and is never modified by the marketplace.

Fields read from `manifest.json`:

| Field | Source |
|-------|--------|
| `id` | `manifest.json → "id"` |
| `version` | `manifest.json → "version"` |
| `install_date` | Last-modified timestamp of the extension root directory |
| `path` | The scanned subdirectory itself |
| `enabled` | Always `true` by default (no persistence yet) |

### 11.3 Extension Manifest Schema

```json
{
  "id": "extension-id",
  "version": "semver",
  "min_plotjuggler_version": "semver",
  "plugins": [
    {
      "name": "PluginClassName",
      "type": "plugin_type",
      "library": "library_name",
      "ui_file": "optional_ui_file.ui"
    }
  ]
}
```

---

## 12. Pending Decisions

| # | Topic | Options | Impact |
|---|-------|---------|--------|
| 1 | ZIP library | QuaZip vs minizip vs libzip | Build complexity |
| 2 | Markdown rendering | QTextBrowser vs plain text | README display |
| 3 | Metrics source | Registry JSON vs GitHub API | Data freshness |
| 4 | Icons | URL in registry vs bundled in ZIP | Download size |
| 5 | Semver parsing | C++ library vs string compare | Correctness |
| 6 | New extension registration | Manual PR vs automated | Developer experience |
| 7 | Pixi timeline | When to add as alternative | Community adoption |
| 8 | Paid plugins | License management approach | Business model |

---

## Document Maintenance

This file should be updated when:
- New requirements are identified
- Requirements are clarified or changed
- Use cases are added or modified
- Constraints change

**Do NOT add:**
- Implementation details
- Code examples
- Architecture decisions (→ ARCHITECTURE.md)
- How-to guides (→ USER_MANUAL.md)
