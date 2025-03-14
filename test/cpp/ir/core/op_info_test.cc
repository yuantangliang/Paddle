// Copyright (c) 2023 PaddlePaddle Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <gtest/gtest.h>

#include "paddle/ir/core/block.h"
#include "paddle/ir/core/builder.h"
#include "paddle/ir/core/builtin_attribute.h"
#include "paddle/ir/core/builtin_op.h"
#include "paddle/ir/core/builtin_type.h"
#include "paddle/ir/core/ir_context.h"
#include "paddle/ir/core/program.h"

TEST(ir_op_info_test, op_op_info_test) {
  ir::IrContext* context = ir::IrContext::Instance();
  ir::Program program(context);

  ir::Block* block = program.block();
  ir::Builder builder(context, block);
  builder.Build<ir::ConstantOp>(ir::Int32_tAttribute::get(context, 5),
                                ir::Int32Type::get(context));

  ir::Operation* op = block->back();

  EXPECT_EQ(block->end(), ++ir::Block::iterator(*op));

  auto& info_map = context->registered_op_info_map();
  EXPECT_FALSE(info_map.empty());

  void* info_1 = op->info().AsOpaquePointer();
  auto info_2 = ir::OpInfo::RecoverFromOpaquePointer(info_1);
  EXPECT_EQ(op->info(), info_2);
}
