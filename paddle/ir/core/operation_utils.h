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

#pragma once

#include "paddle/ir/core/attribute.h"
#include "paddle/ir/core/op_info.h"
#include "paddle/ir/core/region.h"
#include "paddle/ir/core/type.h"
#include "paddle/ir/core/value.h"

namespace ir {

using AttributeMap = std::unordered_map<std::string, Attribute>;

//===----------------------------------------------------------------------===//
// OperationArgument
//===----------------------------------------------------------------------===//

// This represents an operation arguments in an combined form, suitable for use
// with the builder APIs.
struct OperationArgument {
  std::vector<OpResult> inputs;
  AttributeMap attributes;
  std::vector<Type> output_types;
  OpInfo info;
  std::vector<std::unique_ptr<Region>> regions;

 public:
  OperationArgument(IrContext* ir_context, const std::string& name);
  explicit OperationArgument(OpInfo info) : info(info) {}
  OperationArgument(const std::vector<OpResult>& operands,
                    const AttributeMap& attributes,
                    const std::vector<Type>& types,
                    OpInfo info,
                    std::vector<std::unique_ptr<Region>>&& regions = {})
      : inputs(operands),
        attributes(attributes),
        output_types(types),
        info(info),
        regions(std::move(regions)) {}

  template <class InputIt>
  void AddOperands(InputIt first, InputIt last);

  template <class InputIt>
  void AddTypes(InputIt first, InputIt last);

  /// Add an attribute with the specified name.
  void AddAttribute(const std::string& name, Attribute attr) {
    attributes[name] = attr;
  }
  /// Add an array of named attributes.
  template <class InputIt>
  void AddAttributes(InputIt first, InputIt last);
  /// Get the context held by this operation state.
  IrContext* getContext() const { return info.ir_context(); }

  Region* AddRegion() {
    regions.emplace_back(new Region);
    return regions.back().get();
  }
};

template <class InputIt>
void OperationArgument::AddOperands(InputIt first, InputIt last) {
  while (first != last) {
    inputs.emplace_back(*first++);
  }
}
template <class InputIt>
void OperationArgument::AddTypes(InputIt first, InputIt last) {
  while (first != last) {
    output_types.emplace_back(*first++);
  }
}
template <class InputIt>
void OperationArgument::AddAttributes(InputIt first, InputIt last) {
  while (first != last) {
    attributes[first->first] = first->second;
    ++first;
  }
}

}  // namespace ir
