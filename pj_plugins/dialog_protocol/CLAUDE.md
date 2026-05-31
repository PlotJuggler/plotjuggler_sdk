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
- Headers here install into the SAME `pj_plugins/` include tree as the parent
  module (merged at install); keep names distinct.
- `DialogHandle::borrowed()` / `fromBorrowed()` wrap a source/toolbox-owned
  dialog without create/destroy — must not outlive the owning plugin handle.

See ../CLAUDE.md for module context.
