# PlotJuggler Marketplace — Implementation Plan

> **Version:** 1.0.0
> **Last Updated:** 2026-03-05
> **Status:** In Progress
> **Deadline:** 31 March 2026

---

## 1. Project Timeline

A working prototype integrated into PlotJuggler is expected by the end of March / early April 2026.

---

## 2. Sprint Overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│ WEEK 1 (5-11 March): Standalone POC                                     │
│ Deliverable: Qt app with dummy plugins, works on Linux AND Windows     │
│ Note: Dummy plugins only have getMetadata() function (no Qt, no SDK)   │
│ CI Reference: Foxglove MCAP (mono-repo with per-component releases)    │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ WEEK 2 (12-18 March): PlotJuggler Integration                           │
│ Deliverable: Marketplace opens as dialog INSIDE PlotJuggler            │
│ ★ 16 March: Convergence with Davide on real plugin interfaces          │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ WEEK 3 (19-25 March): Real Plugin End-to-End                            │
│ Deliverable: Install REAL plugin from marketplace, works in PJ         │
│ Note: Davide traveling to Japan (work continues autonomously)          │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ WEEK 4 (26-31 March): Polish + Buffer                                   │
│ Deliverable: Demo to Davide, documentation, fixes                      │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Requirements Coverage

### P0 (Minimum Viable) — 100% in March

| ID | Requirement | Week | Status |
|----|-------------|------|--------|
| F-01 | Fetch and parse registry JSON | W1 | ⬜ TODO |
| F-02 | List extensions with cards | W1 | ⬜ TODO |
| F-03 | Search by name, description, tags | W1 | ⬜ TODO |
| F-04 | Filter by category | W1 | ⬜ TODO |
| F-05 | Show extension detail | W1 | ⬜ TODO |
| F-06 | Download ZIP with SHA256 | W1 | ⬜ TODO |
| F-07 | Extract to extensions dir | W1 | ⬜ TODO |
| F-08 | Register in installed.json | W1 | ⬜ TODO |
| F-09 | Detect updates | W3 | ⬜ TODO |
| F-10 | Uninstall extension | W1 | ⬜ TODO |

### Integration (Critical Path)

| ID | Requirement | Week | Status |
|----|-------------|------|--------|
| F-A1 | Menu: Plugins → Marketplace | W2 | ⬜ TODO |
| F-A2 | Hook with plugin loading | W2 | ⬜ TODO |
| F-A3 | Example plugin (CSV Loader) | W3 | ⬜ TODO |
| F-A4 | Test registry on GitHub | W3 | ⬜ TODO |

### Deferred to April+

| ID | Requirement | Reason |
|----|-------------|--------|
| F-11 | Cache with TTL | Direct fetch works |
| F-12 | Backup on updates | V1 can be simple |
| F-13 | Automatic rollback | NOT PRIORITY per Davide (2026-03-05) |
| F-15 | Enable/Disable | Uninstall/reinstall |
| F-16 | Cancel download | Nice-to-have |
| F-17 | Update All | One by one OK |
| F-18 | Confirmation dialogs | If time in W4 |
| F-19-23 | Polish features | Post-MVP |

> **Note (2026-03-05 meeting):** Windows support moved to Week 1. Rollback explicitly deprioritized by Davide.

---

## 4. Week 1: Standalone MVP (5-11 March)

### Daily Breakdown

| Day | Date | Tasks | Deliverable |
|-----|------|-------|-------------|
| Thu | 5 Mar | Setup + Data structs + Attend Data Store presentation 11am | CMake+Qt6, Extension.h |
| Fri | 6 Mar | UI skeleton: MarketplaceWindow + list | Window with splitter |
| Mon | 9 Mar | ExtensionCardDelegate + search | Cards, filter works |
| Tue | 10 Mar | RegistryManager: fetch + parse | Loads from GitHub |
| Wed | 11 Mar | DownloadManager + SHA256 + Zip | Installs dummy ZIP |

### TODO Week 1

- [ ] Create folder `pj_marketplace` in PlotJuggler Core
- [ ] Setup CMakeLists.txt with Qt6, Conan
- [ ] Create Extension.h struct
- [ ] Create InstalledExtension.h struct
- [ ] Create MarketplaceWindow (QMainWindow)
- [ ] Add QSplitter (sidebar + detail)
- [ ] Create ExtensionListWidget with QListView
- [ ] Create ExtensionCardDelegate (custom painting)
- [ ] Add search QLineEdit
- [ ] Add category QComboBox filter
- [ ] Create RegistryManager class
- [ ] Implement fetch with QNetworkAccessManager
- [ ] Implement JSON parsing
- [ ] Create DownloadManager class
- [ ] Implement progress signals
- [ ] Create ChecksumVerifier (SHA256)
- [ ] Create ZipExtractor (QuaZip)
- [ ] Create LocalState (installed.json)
- [ ] Implement install flow
- [ ] Implement uninstall flow
- [ ] Create dummy registry on GitHub for testing
- [ ] Create dummy extension ZIP for testing

### Success Criteria Week 1

- [ ] App opens and shows extensions
- [ ] Search "dummy" finds extension
- [ ] Click Install → downloads → extracts → shows as installed
- [ ] Click Uninstall → removes

---

## 5. Week 2: PlotJuggler Integration (12-18 March)

### Daily Breakdown

| Day | Date | Tasks | Deliverable |
|-----|------|-------|-------------|
| Thu | 12 Mar | Extract marketplace as library | libpj_marketplace.so |
| Fri | 13 Mar | Add entry point in PlotJuggler | Menu item |
| Mon | 16 Mar | Integrate as QDialog | Opens inside PJ |
| Tue | 17 Mar | Hook with plugin loader | PJ detects installed |
| Wed | 18 Mar | Testing + fixes | Full flow in PJ |

### TODO Week 2

- [ ] Refactor standalone → library
- [ ] Create MarketplaceDialog (QDialog wrapper)
- [ ] Add menu action in PlotJuggler
- [ ] Connect to plugin loading system
- [ ] Handle "restart required" case
- [ ] Test install flow from inside PJ
- [ ] Test uninstall flow from inside PJ
- [ ] Fix integration issues

### Success Criteria Week 2

- [ ] PlotJuggler: Plugins → Marketplace works
- [ ] Dialog shows marketplace UI
- [ ] Can install extension from inside PJ
- [ ] Extension appears in correct directory

---

## 6. Week 3: Real Plugin End-to-End (19-25 March)

### Daily Breakdown

| Day | Date | Tasks | Deliverable |
|-----|------|-------|-------------|
| Thu | 19 Mar | Create example plugin: CSV Loader | Minimal plugin |
| Fri | 20 Mar | Package as ZIP with manifest | csv-loader.zip |
| Mon | 23 Mar | Publish to test registry | GitHub Release |
| Tue | 24 Mar | Test: install from marketplace | Plugin appears |
| Wed | 25 Mar | Test: use the plugin | Load CSV file |

### TODO Week 3

- [ ] Create SimpleCsvLoader plugin (~100 lines)
- [ ] Create manifest.json for it
- [ ] Package as ZIP
- [ ] Create GitHub repo for test registry
- [ ] Create registry.json with csv-loader
- [ ] Upload ZIP as GitHub Release
- [ ] Test: marketplace shows csv-loader
- [ ] Test: install downloads and extracts
- [ ] Test: restart PJ, plugin loads
- [ ] Test: load a CSV file with plugin
- [ ] Implement update detection (F-09)

### Success Criteria Week 3

- [ ] CSV Loader appears in marketplace
- [ ] Click Install → downloads and installs
- [ ] Restart PlotJuggler → plugin available
- [ ] Load CSV file → data appears

---

## 7. Week 4: Polish + Buffer (26-31 March)

### Daily Breakdown

| Day | Date | Tasks | Deliverable |
|-----|------|-------|-------------|
| Thu | 26 Mar | Bug fixes from testing | Stability |
| Fri | 27 Mar | Error messages UX | Better feedback |
| Mon | 30 Mar | README + documentation | Docs |
| Tue | 31 Mar | **DEMO TO DAVIDE** | Presentation |

### TODO Week 4

- [ ] Fix all known bugs
- [ ] Improve error messages
- [ ] Add confirmation dialogs (F-18) if time
- [ ] Write README for pj_marketplace
- [ ] Document how to add extensions
- [ ] Prepare demo script
- [ ] **Demo to Davide**

### Demo Checklist

- [ ] Open PlotJuggler
- [ ] Go to Plugins → Marketplace
- [ ] See list of extensions
- [ ] Search for "csv"
- [ ] Install CSV Loader
- [ ] Close marketplace
- [ ] Verify plugin is available
- [ ] Load a CSV file
- [ ] Show data in PlotJuggler

---

## 8. Risks and Mitigations

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Qt6/Conan setup complex | Medium | High | Use Davide's monorepo config |
| PJ integration harder than expected | High | High | Start Week 2 early, ask Davide |
| Plugin SDK not ready | Medium | High | Use existing plugin as base |
| Scope creep | High | High | This document is the scope. NO more |
| Bugs in Week 4 | Medium | Medium | Week 4 is buffer, not features |

---

## 9. Communication Plan

### Check-ins with Davide

| Date | Demo | Content |
|------|------|---------|
| 11 Mar | Week 1 | "Standalone works" |
| 18 Mar | Week 2 | "Now inside PlotJuggler" |
| 25 Mar | Week 3 | "Real plugin installed from marketplace" |
| 31 Mar | Final | "Complete prototype" |

### Daily Standups

- **Time:** 10am daily
- **Format:** 2 min max
  1. Yesterday: what completed
  2. Today: what working on
  3. Blockers: if any

### If Blocked

1. Communicate immediately
2. Propose alternative
3. Adjust scope if needed

---

## 10. Definition of Done

### For Week 1 (Standalone MVP)
- [ ] Code compiles on Linux
- [ ] All TODO items checked
- [ ] Success criteria met
- [ ] Committed to repo

### For Week 2 (Integration)
- [ ] Marketplace opens from PJ menu
- [ ] Install works from inside PJ
- [ ] Code reviewed by Davide

### For Week 3 (End-to-End)
- [ ] Real plugin installs and works
- [ ] Test registry published
- [ ] Documented

### For Final Demo
- [ ] Demo script executed successfully
- [ ] Davide approves
- [ ] No critical bugs

---

## 11. Post-March Roadmap (April+)

After the March deadline, these items can be addressed:

1. **Windows support** — Staging system
2. **macOS support** — Testing and fixes
3. **Rollback** — Automatic restoration
4. **Cache** — Registry caching with TTL
5. **CI Template** — For external developers
6. **Polish** — Icons, changelog, metrics

---

## Document Maintenance

- Update TODO checkboxes as work progresses
- Add new items if discovered during implementation
- Move completed items to "Done" section
- **Delete this file when project is complete**

---

## Done

*(Move completed items here)*

### Week 1
- (none yet)

### Week 2
- (none yet)

### Week 3
- (none yet)

### Week 4
- (none yet)
