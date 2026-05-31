#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MPL-2.0

// Built-in ISISOTransform implementations registerable with DerivedEngine.
// See derived_engine.hpp for the sequential calculate()/reset() contract.

#include "pj_base/types.hpp"
#include "pj_datastore/derived_engine.hpp"

namespace PJ {

// ---------------------------------------------------------------------------
// DerivativeTransform
// ---------------------------------------------------------------------------
// Numerical derivative: d(value)/d(t) in units/second.
// Skips the first row (no previous sample). Assumes float64 input/output.
class DerivativeTransform : public ISISOTransform {
  PJ::Timestamp prev_time_ = 0;
  double prev_value_ = 0.0;
  bool has_prev_ = false;

 public:
  void reset() override;

  [[nodiscard]] StorageKind outputKind(StorageKind input_kind) const override;

  bool calculate(PJ::Timestamp time, const VarValue& input, PJ::Timestamp& out_time, VarValue& out_value) override;
};

}  // namespace PJ
