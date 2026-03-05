# PlotJuggler Marketplace — Sprint Proposal

> **Target:** Integrated prototype by end of March / early April 2026
> **Owner:** Pablo (IBRobotics)

---

## 1. Aggressive Prioritization: What's IN and What's OUT

### MUST HAVE (March - 4 weeks)

| # | Feature | Why Critical |
|---|---------|--------------|
| 1 | Fetch registry JSON | Nothing works without this |
| 2 | Show extension list | Minimum UX |
| 3 | Search and filter | Basic usability |
| 4 | Install extension (download + extract) | Core value |
| 5 | Verify checksum | Minimum security |
| 6 | Detect updates | Value proposition |
| 7 | Uninstall | Complete flow |
| 8 | **INTEGRATION in PlotJuggler** | Month's goal |
| 9 | 1 working dummy plugin | End-to-end proof |

### DEFERRED (April+)

| Feature | Why It Can Wait |
|---------|-----------------|
| Windows staging | Ship Linux first |
| Automatic rollback | Nice-to-have, not critical for demo |
| Enable/Disable | Can uninstall/reinstall |
| Local cache with TTL | Direct network fetch works |
| Backup on updates | First version simple |
| Extension icons | Text works |
| Changelog UI | README is enough |
| Multiple registries | One registry suffices |
| Complete GitHub CI Template | Manual is OK for beta |
| Metrics (downloads, rating) | Later phase |

---

## 2. High-Level View (4 Weeks)

```
┌─────────────────────────────────────────────────────────────────────────┐
│ WEEK 1 (5-11 March): Standalone MVP                                     │
│ Deliverable: Qt app that loads registry, shows list, installs dummy    │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ WEEK 2 (12-18 March): PlotJuggler Integration                           │
│ Deliverable: Marketplace opens as dialog INSIDE PlotJuggler            │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ WEEK 3 (19-25 March): Real Plugin End-to-End                            │
│ Deliverable: Install REAL plugin from marketplace, works in PJ         │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ WEEK 4 (26-31 March): Polish + Buffer                                   │
│ Deliverable: Demo to Davide, documentation, fixes                      │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Week 1: Standalone MVP (5-11 March)

### Requirements to Implement

| ID | Requirement |
|----|-------------|
| F-01 | Fetch and parse registry JSON from configurable URL |
| F-02 | List extensions in sidebar with cards |
| F-03 | Search by name, description, tags |
| F-04 | Filter by category |
| F-05 | Show selected extension detail |
| F-06 | Download ZIP with SHA256 verification |
| F-07 | Extract ZIP to extensions directory |
| F-08 | Register installed extension (installed.json) |
| F-10 | Uninstall extension |

### Daily Breakdown

| Day | Date | Main Task | Deliverable |
|-----|------|-----------|-------------|
| Thu | 5 Mar | Setup + Data structs + Attend Data Store presentation 11am | CMake+Qt6 working, Extension.h |
| Fri | 6 Mar | UI skeleton: MarketplaceWindow + list | Window with splitter |
| Mon | 9 Mar | ExtensionCardDelegate + search | Nice cards, filter works |
| Tue | 10 Mar | RegistryManager: fetch + parse JSON | Loads from GitHub |
| Wed | 11 Mar | DownloadManager + SHA256 + ZipExtractor | Installs dummy ZIP |

### Success Criteria Week 1

- [ ] App opens and shows extensions from GitHub
- [ ] Can search "dummy" and find the extension
- [ ] Click "Install" → downloads → extracts → appears as installed
- [ ] Click "Uninstall" → removed

---

## 4. Week 2: PlotJuggler Integration (12-18 March)

### Requirements to Implement

| ID | Requirement |
|----|-------------|
| F-A1 | Integration in PlotJuggler (Plugins → Marketplace menu) |
| F-A2 | Hook with PlotJuggler's plugin loading system |

### Daily Breakdown

| Day | Date | Main Task | Deliverable |
|-----|------|-----------|-------------|
| Thu | 12 Mar | Extract marketplace as library | libpj_marketplace.so |
| Fri | 13 Mar | Create entry point in PlotJuggler | Menu: Plugins → Marketplace |
| Mon | 16 Mar | Integrated modal dialog | Opens as QDialog inside PJ |
| Tue | 17 Mar | Hook with plugin loading system | PJ detects installed plugins |
| Wed | 18 Mar | Integration testing + fixes | Full flow inside PJ |

### Success Criteria Week 2

- [ ] From PlotJuggler: Plugins → Marketplace works
- [ ] Dialog opens with marketplace UI
- [ ] Can install an extension from inside PJ
- [ ] Installed extension appears in correct directory

---

## 5. Week 3: Real Plugin End-to-End (19-25 March)

### Requirements to Implement

| ID | Requirement |
|----|-------------|
| F-09 | Detect updates (local vs registry version) |
| F-A3 | Functional example plugin (CSV Loader) |
| F-A4 | Test registry on GitHub |

### Daily Breakdown

| Day | Date | Main Task | Deliverable |
|-----|------|-----------|-------------|
| Thu | 19 Mar | Create example plugin: Simple CSV Loader | Minimal plugin |
| Fri | 20 Mar | Package as ZIP with manifest | csv-loader-linux-x86_64.zip |
| Mon | 23 Mar | Publish to test registry | GitHub Release + registry.json |
| Tue | 24 Mar | Testing: install from marketplace | Plugin appears in PJ |
| Wed | 25 Mar | Testing: use the plugin | Load a real CSV file |

### Success Criteria Week 3

- [ ] CSV Loader appears in marketplace
- [ ] Click Install → downloads and installs
- [ ] Restart PlotJuggler → plugin is available
- [ ] Load a CSV file → data appears in PlotJuggler

---

## 6. Week 4: Polish + Buffer (26-31 March)

### Requirements to Implement (if time permits)

| ID | Requirement | Priority |
|----|-------------|----------|
| F-16 | Cancel download in progress | ⚠️ Nice-to-have |
| F-18 | Confirmation dialogs | ⚠️ Nice-to-have |
| - | Bug fixes and edge cases | 🎯 Critical |
| - | Minimal documentation | 🎯 Critical |

### Daily Breakdown

| Day | Date | Main Task | Deliverable |
|-----|------|-----------|-------------|
| Thu | 26 Mar | Fix bugs found in testing | Stability |
| Fri | 27 Mar | Improve error messages | UX |
| Mon | 30 Mar | Write README + documentation | Minimal docs |
| Tue | 31 Mar | **DEMO TO DAVIDE** | Presentation |

### Demo Checklist

- [ ] Open PlotJuggler
- [ ] Go to Plugins → Marketplace
- [ ] See extension list
- [ ] Search for "csv"
- [ ] Install CSV Loader
- [ ] Close marketplace
- [ ] Verify plugin is available
- [ ] Load a CSV file
- [ ] Show data in PlotJuggler

---

## 7. Requirements Coverage Summary

### P0 (Minimum Viable) — 100% in March

| ID | Requirement | Week | Status |
|----|-------------|------|--------|
| F-01 | Fetch and parse registry JSON | W1 | ⬜ |
| F-02 | List extensions with cards | W1 | ⬜ |
| F-03 | Search by name, description, tags | W1 | ⬜ |
| F-04 | Filter by category | W1 | ⬜ |
| F-05 | Show extension detail | W1 | ⬜ |
| F-06 | Download ZIP with SHA256 | W1 | ⬜ |
| F-07 | Extract to extensions dir | W1 | ⬜ |
| F-08 | Register in installed.json | W1 | ⬜ |
| F-09 | Detect updates | W3 | ⬜ |
| F-10 | Uninstall extension | W1 | ⬜ |

### Integration (Critical Path)

| ID | Requirement | Week | Status |
|----|-------------|------|--------|
| F-A1 | Menu: Plugins → Marketplace | W2 | ⬜ |
| F-A2 | Hook with plugin loading | W2 | ⬜ |
| F-A3 | Example plugin (CSV Loader) | W3 | ⬜ |
| F-A4 | Test registry on GitHub | W3 | ⬜ |

### Coverage Summary

```
MARCH TOTAL:  10/10 P0 (100%)
              0-2/8 P1 (0-25%)
              0/5 P2 (0%)
              4/4 Additional (100%)
```

---

## 8. Risks and Mitigations

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Qt6/Conan setup complex | Medium | High | Use Davide's monorepo config |
| PJ integration harder than expected | High | High | Start Week 2 early, ask Davide for help |
| Plugin SDK not ready | Medium | High | Use existing plugin as base |
| Scope creep | High | High | This document IS the scope. NO more features |
| Bugs in Week 4 | Medium | Medium | Week 4 is buffer, not features |

---

## 9. Communication Plan

### Check-ins with Davide

| Date | Milestone | Content |
|------|-----------|---------|
| 11 Mar | Week 1 | "Look, it works standalone" |
| 18 Mar | Week 2 | "Now it's inside PlotJuggler" |
| 25 Mar | Week 3 | "This plugin was installed from the marketplace" |
| 31 Mar | Final | "Here's the complete prototype" |

### If Something Goes Wrong

1. **Communicate immediately** — Don't wait for problems to pile up
2. **Propose alternative** — Not just the problem, also the solution
3. **Adjust scope** — Better to deliver less but working

---

## 10. What's NOT in This Plan (and That's OK)

1. **Windows**: Linux only. macOS/Windows in phase 2 (April)
2. **Automatic rollback**: Manual is OK for v1
3. **Enable/Disable**: Uninstall/reinstall works
4. **Icons**: Text only
5. **Changelog UI**: README in details panel
6. **Multiple registries**: One hardcoded registry
7. **Complete CI Template**: Manual documentation
8. **Sophisticated cache**: Direct fetch every time

---

## 11. Success Metrics

### End of March

- [ ] Working prototype available
- [ ] Real plugin installs and works
- [ ] Integration in PlotJuggler complete
- [ ] Demo executed successfully

### Quantitative

- 10/10 P0 requirements implemented
- 4/4 integration requirements implemented
- 1 real plugin working end-to-end
