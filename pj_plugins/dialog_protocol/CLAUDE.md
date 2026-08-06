# pj_plugins/dialog_protocol — Dialog plugin C ABI, C++ SDK, and host loader

A self-contained nested module: the toolkit-neutral Dialog C ABI
(`dialog_protocol.h`), the C++ dialog SDK (`sdk/DialogPluginTyped`, `WidgetData`,
`WidgetEvent`), and the host side (`host/DialogLibrary`, `host/DialogHandle`,
`WidgetDataView`, `WidgetEventBuilder`). Plugins link `pj_dialog_sdk` only — no
Qt; the GUI host renders the `.ui` XML and relays events over the vtable.

Local traps not visible from the headers:
- The `QDialogButtonBox` MUST be named `buttonBox` AND set `standardButtons` in
  the XML, or the dialog renders with no OK/Cancel and no compile error.
- `QTextEdit` / model-based `QTableView` are unsupported by the widget binding —
  use `QPlainTextEdit` / `QTableWidget`. See `../docs/dialog-plugin-guide.md`.
- `QStackedWidget` pages are addressed by each direct child page's Qt
  `objectName`, not its label or position. `stacked_page` wins over a simultaneous
  `stacked_index`; empty/unknown names and negative/out-of-range indexes are
  invalid host-side warning/no-ops. See `../docs/dialog-plugin-guide.md`.
- `QTreeWidget` items are addressed only by stable plugin-supplied string IDs,
  in a flat `id` / `parent_id` full snapshot. Array order defines unsorted
  sibling order; check state is column 0 only. Visibility is tri-state:
  absent/unchanged, ID array/filter (empty hides all), or JSON null/reset; the
  host adds visible ancestors and keeps selection logical across filtering.
  Header and ID arrays are strict: any non-string rejects that whole channel as
  unchanged, so malformed arrays never become destructive clears. Activation
  columns must be integers in `0..INT_MAX`.
  `may_have_children` supports a private placeholder followed by a later full
  snapshot. Tree deltas are deferred; any future delta reuses the table
  sequence/all-or-nothing contract. See `../docs/dialog-plugin-guide.md`.
- Structured file pickers use stable filter IDs because native display text may
  be localized or normalized. As with tree/stacked, the writer serializes
  caller input and `WidgetDataView` atomically rejects empty/duplicate IDs,
  missing selected IDs, or empty patterns. The normative dispatch priority,
  compatibility-key validation, and malformed/mixed-event rules live only in
  `../../docs/dialog-sdk-reference.md` → "Event dispatch priority and
  validation". `onClicked` precedes the host picker and result; file work
  belongs in `onFilePickerResult`. See `../docs/dialog-plugin-guide.md`.
- A table must not combine `sortingEnabled=true` in its `.ui` XML with
  `onHeaderClicked`: Qt sorts the view while the plugin reorders the model and the
  plugin's order loses. Sort keys (`setTableRows` with `TableItem`) or
  `onHeaderClicked` — one per table, never both.
- Sortable tables must also leave item drag/drop (`dragEnabled`, `InternalMove`)
  OFF: Qt reconstructs dropped cells from serialized display roles, which strips
  the typed sort key and leaves a column mixing keyed and keyless cells.
- Headers here install into the SAME `pj_plugins/` include tree as the parent
  module (merged at install); keep names distinct.
- `DialogHandle::borrowed()` / `fromBorrowed()` wrap a source/toolbox-owned
  dialog without create/destroy — must not outlive the owning plugin handle.

See ../CLAUDE.md for module context.
