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

#include "paddle/phi/kernels/nanmedian_grad_kernel.h"

#include "paddle/phi/backends/cpu/cpu_context.h"
#include "paddle/phi/core/kernel_registry.h"
#include "paddle/phi/kernels/funcs/math_function.h"
#include "paddle/phi/kernels/impl/nanmedian_grad_kernel_impl.h"

namespace phi {

template <typename T, typename Context>
void CalcMedianGradKernel(const Context& dev_ctx,
                          const DenseTensor& x,
                          const DenseTensor& median_index,
                          const DenseTensor& out_grad,
                          const IntArray& axes UNUSED,
                          DenseTensor* x_grad,
                          T* x_grad_ptr) {
  phi::funcs::SetConstant<Context, T> set_zero;
  set_zero(dev_ctx, x_grad, static_cast<T>(0));
  if (!x_grad_ptr) return;

  const int64_t* m_ptr = median_index.data<int64_t>();
  const T* out_grad_ptr = out_grad.data<T>();
  int64_t numel = x.numel();
  auto x_dim = x.dims();
  int64_t rank = x_dim.size();
  int64_t stride = x_dim[rank - 1];

  int64_t pre_dim = numel / stride;
  int64_t i = 0;
  int64_t offset = 0;
  T div_factor = static_cast<T>(2.0);
  for (i = 0; i < pre_dim; i++) {
    if (m_ptr[2 * i] >= 0) {
      if (m_ptr[2 * i] == m_ptr[2 * i + 1]) {
        x_grad_ptr[offset + m_ptr[2 * i]] = out_grad_ptr[i];
      } else {
        x_grad_ptr[offset + m_ptr[2 * i]] = out_grad_ptr[i] / div_factor;
        x_grad_ptr[offset + m_ptr[2 * i + 1]] = out_grad_ptr[i] / div_factor;
      }
    }
    offset += stride;
  }
}

template <typename T, typename Context>
void BaseMedianGradKernel(const Context& dev_ctx,
                          const DenseTensor& x,
                          const DenseTensor& median_index,
                          const DenseTensor& out_grad,
                          const IntArray& axes,
                          DenseTensor* x_grad) {
  auto rank = x.dims().size();
  T* x_grad_ptr = dev_ctx.template Alloc<T>(x_grad);
  if (axes.size() && (rank > 1)) {
    DenseTensor tmp_x_grad(*x_grad);
    CalcMedianGradKernel<T, Context>(
        dev_ctx, x, median_index, out_grad, axes, &tmp_x_grad, x_grad_ptr);
    PostprocessMedianGradKernel<T, Context>(dev_ctx, &tmp_x_grad, axes, x_grad);
  } else {
    CalcMedianGradKernel<T, Context>(
        dev_ctx, x, median_index, out_grad, axes, x_grad, x_grad_ptr);
  }
}

template <typename T, typename Context>
void NanmedianGradKernel(const Context& dev_ctx,
                         const DenseTensor& input,
                         const DenseTensor& median_index,
                         const DenseTensor& out_grad,
                         const IntArray& axes,
                         bool keep_dim UNUSED,
                         DenseTensor* x_grad) {
  BaseMedianGradKernel<T, Context>(
      dev_ctx, input, median_index, out_grad, axes, x_grad);
}

}  // namespace phi

PD_REGISTER_KERNEL(nanmedian_grad,
                   CPU,
                   ALL_LAYOUT,
                   phi::NanmedianGradKernel,
                   float,
                   double,
                   int,
                   int64_t) {}
