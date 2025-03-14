// Copyright (c) 2022 PaddlePaddle Authors. All Rights Reserved.
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

#include "paddle/phi/core/compat/op_utils.h"

namespace phi {
KernelSignature FillOpArgumentMapping(
    const ArgumentMappingContext& ctx UNUSED) {
  return KernelSignature("fill", {"X"}, {"value_float"}, {"Out"});
}

KernelSignature FillGradOpArgumentMapping(
    const ArgumentMappingContext& ctx UNUSED) {
  return KernelSignature(
      "fill_grad", {"Out@GRAD"}, {"value_float"}, {"X@GRAD"});
}

}  // namespace phi

PD_REGISTER_BASE_KERNEL_NAME(fill_any, fill);
PD_REGISTER_BASE_KERNEL_NAME(fill_any_grad, fill_grad);

PD_REGISTER_ARG_MAPPING_FN(fill_any, phi::FillOpArgumentMapping);
PD_REGISTER_ARG_MAPPING_FN(fill_any_grad, phi::FillGradOpArgumentMapping);
