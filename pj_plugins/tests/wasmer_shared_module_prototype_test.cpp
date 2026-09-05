// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <wasmer.h>

#include <array>
#include <cstdint>
#include <thread>

namespace PJ {
namespace {

// (module
//   (global (mut i32) (i32.const 0))
//   (func (export "next_value") (result i32)
//     global.get 0 i32.const 1 i32.add global.set 0 global.get 0))
constexpr std::array<uint8_t, 58> kCounterModule{
    0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00,                                                  // preamble
    0x01, 0x05, 0x01, 0x60, 0x00, 0x01, 0x7F,                                                        // type
    0x03, 0x02, 0x01, 0x00,                                                                          // function
    0x06, 0x06, 0x01, 0x7F, 0x01, 0x41, 0x00, 0x0B,                                                  // mutable global
    0x07, 0x0E, 0x01, 0x0A, 'n',  'e',  'x',  't',  '_',  'v',                                       // export
    'a',  'l',  'u',  'e',  0x00, 0x00, 0x0A, 0x0D, 0x01, 0x0B, 0x00, 0x23, 0x00, 0x41, 0x01, 0x6A,  // code
    0x24, 0x00, 0x23, 0x00, 0x0B,
};

struct StoreInstance {
  ~StoreInstance() {
    if (exports_initialized) {
      wasm_extern_vec_delete(&exports);
    }
    if (instance != nullptr) {
      wasm_instance_delete(instance);
    }
    if (store != nullptr) {
      wasm_store_delete(store);
    }
  }

  wasm_store_t* store = nullptr;
  wasm_instance_t* instance = nullptr;
  wasm_extern_vec_t exports = WASM_EMPTY_VEC;
  bool exports_initialized = false;
  wasm_func_t* next = nullptr;
};

bool instantiate(wasm_engine_t* engine, wasm_module_t* module, StoreInstance* output) {
  output->store = wasm_store_new(engine);
  if (output->store == nullptr) {
    return false;
  }
  const wasm_extern_vec_t imports = WASM_EMPTY_VEC;
  output->instance = wasm_instance_new(output->store, module, &imports, nullptr);
  if (output->instance == nullptr) {
    return false;
  }
  wasm_instance_exports(output->instance, &output->exports);
  output->exports_initialized = true;
  if (output->exports.size != 1) {
    return false;
  }
  output->next = wasm_extern_as_func(output->exports.data[0]);
  return output->next != nullptr;
}

bool nextValue(wasm_func_t* function, int32_t* output) {
  const wasm_val_vec_t arguments = WASM_EMPTY_VEC;
  wasm_val_t result_values[1] = {WASM_INIT_VAL};
  wasm_val_vec_t results = WASM_ARRAY_VEC(result_values);
  wasm_trap_t* trap = wasm_func_call(function, &arguments, &results);
  if (trap != nullptr) {
    wasm_trap_delete(trap);
    return false;
  }
  if (result_values[0].kind != WASM_I32) {
    return false;
  }
  *output = result_values[0].of.i32;
  return true;
}

TEST(WasmerSharedModulePrototype, CompilesOnceAndInstantiatesIntoIndependentStores) {
  wasm_engine_t* engine = wasm_engine_new();
  ASSERT_NE(engine, nullptr);
  const wasm_byte_vec_t binary{
      .size = kCounterModule.size(),
      .data = reinterpret_cast<wasm_byte_t*>(const_cast<uint8_t*>(kCounterModule.data())),
  };
  wasm_module_t* module = wasmer_module_new(engine, &binary);
  ASSERT_NE(module, nullptr);

  {
    StoreInstance first;
    StoreInstance second;
    ASSERT_TRUE(instantiate(engine, module, &first));
    ASSERT_TRUE(instantiate(engine, module, &second));
    int32_t first_value = 0;
    int32_t second_value = 0;
    EXPECT_TRUE(nextValue(first.next, &first_value));
    EXPECT_TRUE(nextValue(second.next, &second_value));
    EXPECT_EQ(first_value, 1);
    EXPECT_EQ(second_value, 1);
  }

  wasm_module_delete(module);
  wasm_engine_delete(engine);
}

TEST(WasmerSharedModulePrototype, AllowsSequentialCallFromNonCreatorThread) {
  wasm_engine_t* engine = wasm_engine_new();
  ASSERT_NE(engine, nullptr);
  const wasm_byte_vec_t binary{
      .size = kCounterModule.size(),
      .data = reinterpret_cast<wasm_byte_t*>(const_cast<uint8_t*>(kCounterModule.data())),
  };
  wasm_module_t* module = wasmer_module_new(engine, &binary);
  ASSERT_NE(module, nullptr);
  {
    StoreInstance instance;
    ASSERT_TRUE(instantiate(engine, module, &instance));

    int32_t creator_value = 0;
    ASSERT_TRUE(nextValue(instance.next, &creator_value));
    int32_t other_thread_value = 0;
    bool other_thread_ok = false;
    std::thread caller([&] { other_thread_ok = nextValue(instance.next, &other_thread_value); });
    caller.join();
    int32_t returned_value = 0;
    EXPECT_TRUE(nextValue(instance.next, &returned_value));
    EXPECT_TRUE(other_thread_ok);
    EXPECT_EQ(creator_value, 1);
    EXPECT_EQ(other_thread_value, 2);
    EXPECT_EQ(returned_value, 3);
  }

  wasm_module_delete(module);
  wasm_engine_delete(engine);
}

}  // namespace
}  // namespace PJ
