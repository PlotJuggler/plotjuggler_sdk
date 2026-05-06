#pragma once

#include "pj_base/data_source_protocol.h"
#include "pj_base/expected.hpp"
#include "pj_base/message_parser_protocol.h"
#include "pj_base/toolbox_protocol.h"

namespace PJ::detail {

Status validateRequiredSlots(const PJ_data_source_vtable_t* vtable);
Status validateRequiredSlots(const PJ_message_parser_vtable_t* vtable);
Status validateRequiredSlots(const PJ_toolbox_vtable_t* vtable);

}  // namespace PJ::detail
