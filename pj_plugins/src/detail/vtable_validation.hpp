#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/data_source_protocol.h"
#include "pj_base/expected.hpp"
#include "pj_base/message_parser_protocol.h"
#include "pj_base/toolbox_protocol.h"
#include "pj_plugins/dialog_protocol.h"

namespace PJ::detail {

Status validateRequiredSlots(const PJ_data_source_vtable_t* vtable);
Status validateRequiredSlots(const PJ_message_parser_vtable_t* vtable);
Status validateRequiredSlots(const PJ_toolbox_vtable_t* vtable);
Status validateRequiredSlots(const PJ_dialog_vtable_t* vtable);

}  // namespace PJ::detail
