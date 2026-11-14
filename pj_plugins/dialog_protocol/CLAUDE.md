# pj_plugins/dialog_protocol — Dialog plugin C ABI, C++ SDK, and host loader

This self-contained nested module contains the toolkit-neutral Dialog C ABI
(`dialog_protocol.h`) and C++ SDK (`sdk/DialogPluginTyped`, `WidgetData`,
`WidgetEvent`). Host support includes `host/DialogLibrary`, `host/DialogHandle`,
`WidgetDataView` and `WidgetEventBuilder`.

Plugins link only `pj_dialog_sdk` (inside `plotjuggler_sdk::plugin_sdk`), with
no Qt. The GUI host renders the `.ui` XML and relays events over the vtable.
Author guide: [`../docs/dialog-plugin-guide.md`](../docs/dialog-plugin-guide.md);
setter and event reference: [`../../docs/dialog-sdk-reference.md`](../../docs/dialog-sdk-reference.md),
whose "Event dispatch priority and validation" section is the only normative
statement of dispatch order and malformed-event handling.

Local traps not visible from the headers:
- The `QDialogButtonBox` MUST be named `buttonBox` AND set `standardButtons` in
  the XML, or the dialog renders with no OK/Cancel and no compile error.
- `QTextEdit` / model-based `QTableView` are unsupported by the widget binding —
  use `QPlainTextEdit` / `QTableWidget`.
- `QStackedWidget` pages are addressed by each direct child page's `objectName`,
  not its label or position.
- `QTreeWidget` items are addressed only by stable plugin-supplied string IDs in
  a flat full snapshot; a malformed header or ID array is rejected whole, never
  read as a clear. Rules: the guide's "Tree Widgets" section.
- File pickers use stable filter IDs. `onClicked` runs before the host picker;
  put file work in `onFilePickerResult`.
- A table uses either sort keys (`setTableRows` with `TableItem`) or
  `onHeaderClicked`, never both, and never with `sortingEnabled=true` in the
  `.ui` together with `onHeaderClicked`. Sortable tables leave item drag/drop
  (`dragEnabled`, `InternalMove`) off: Qt rebuilds dropped cells from display
  text and drops the sort key.
- Headers here install into the SAME `pj_plugins/` include tree as the parent
  module (merged at install); keep names distinct.
- `DialogHandle::borrowed()` / `fromBorrowed()` wrap a source/toolbox-owned
  dialog without create/destroy — must not outlive the owning plugin handle.

See ../CLAUDE.md for module context.
