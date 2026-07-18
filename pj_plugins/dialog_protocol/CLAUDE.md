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
