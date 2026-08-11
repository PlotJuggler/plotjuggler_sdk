// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/plugin_abi_export.hpp"

#if defined(_MSC_VER)
// PE forwarder for the family getter: the export resolves at GetProcAddress
// time from entry_point_donor.dll. The /export directive form is required
// because MSVC LINK parses the equivalent .def EXPORTS entry as an
// internal-name alias and demands a local definition (LNK2001).
#pragma comment(linker, "/export:PJ_get_data_source_vtable=entry_point_donor.PJ_get_data_source_vtable")
#endif
