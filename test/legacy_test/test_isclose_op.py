#   Copyright (c) 2021 PaddlePaddle Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import unittest

import numpy as np
from eager_op_test import OpTest

import paddle
from paddle.fluid import core


class TestIscloseOp(OpTest):
    def set_args(self):
        self.input = np.array([10000.0, 1e-07]).astype("float32")
        self.other = np.array([10000.1, 1e-08]).astype("float32")
        self.rtol = np.array([1e-05]).astype("float64")
        self.atol = np.array([1e-08]).astype("float64")
        self.equal_nan = False

    def setUp(self):
        paddle.enable_static()
        self.set_args()
        self.op_type = "isclose"
        self.python_api = paddle.isclose
        self.inputs = {
            'Input': self.input,
            'Other': self.other,
            "Rtol": self.rtol,
            "Atol": self.atol,
        }
        self.attrs = {'equal_nan': self.equal_nan}
        self.outputs = {
            'Out': np.isclose(
                self.inputs['Input'],
                self.inputs['Other'],
                rtol=self.rtol,
                atol=self.atol,
                equal_nan=self.equal_nan,
            )
        }

    def test_check_output(self):
        self.check_output()


class TestIscloseOpException(TestIscloseOp):
    def test_check_output(self):
        def test_rtol_num():
            self.inputs['Rtol'] = np.array([1e-05, 1e-05]).astype("float64")
            self.inputs['Atol'] = np.array([1e-08]).astype("float64")
            self.check_output()

        self.assertRaises(ValueError, test_rtol_num)

        def test_rtol_type():
            self.inputs['Rtol'] = np.array([5]).astype("int32")
            self.inputs['Atol'] = np.array([1e-08]).astype("float64")
            self.check_output()

        self.assertRaises(ValueError, test_rtol_type)

        def test_atol_num():
            self.inputs['Rtol'] = np.array([1e-05]).astype("float64")
            self.inputs['Atol'] = np.array([1e-08, 1e-08]).astype("float64")
            self.check_output()

        self.assertRaises(ValueError, test_atol_num)

        def test_atol_type():
            self.inputs['Rtol'] = np.array([1e-05]).astype("float64")
            self.inputs['Atol'] = np.array([8]).astype("int32")
            self.check_output()

        self.assertRaises(ValueError, test_atol_type)


class TestIscloseOpSmallNum(TestIscloseOp):
    def set_args(self):
        self.input = np.array([10000.0, 1e-08]).astype("float32")
        self.other = np.array([10000.1, 1e-09]).astype("float32")
        self.rtol = np.array([1e-05]).astype("float64")
        self.atol = np.array([1e-08]).astype("float64")
        self.equal_nan = False


class TestIscloseOpNanFalse(TestIscloseOp):
    def set_args(self):
        self.input = np.array([1.0, float('nan')]).astype("float32")
        self.other = np.array([1.0, float('nan')]).astype("float32")
        self.rtol = np.array([1e-05]).astype("float64")
        self.atol = np.array([1e-08]).astype("float64")
        self.equal_nan = False


class TestIscloseOpNanTrue(TestIscloseOp):
    def set_args(self):
        self.input = np.array([1.0, float('nan')]).astype("float32")
        self.other = np.array([1.0, float('nan')]).astype("float32")
        self.rtol = np.array([1e-05]).astype("float64")
        self.atol = np.array([1e-08]).astype("float64")
        self.equal_nan = True


class TestIscloseStatic(unittest.TestCase):
    def test_api_case(self):
        paddle.enable_static()
        x_data = np.random.rand(10, 10)
        y_data = np.random.rand(10, 10)
        places = [paddle.fluid.CPUPlace()]
        if paddle.fluid.core.is_compiled_with_cuda():
            places.append(paddle.fluid.CUDAPlace(0))
        for place in places:
            with paddle.static.program_guard(
                paddle.static.Program(), paddle.static.Program()
            ):
                x = paddle.static.data(
                    name='x', shape=[10, 10], dtype='float64'
                )
                y = paddle.static.data(
                    name='y', shape=[10, 10], dtype='float64'
                )
                result = paddle.isclose(x, y)
                exe = paddle.fluid.Executor(place)
                fetches = exe.run(
                    paddle.fluid.default_main_program(),
                    feed={"x": x_data, "y": y_data},
                    fetch_list=[result],
                )
                expected_out = np.isclose(x_data, y_data)
                self.assertTrue((fetches[0] == expected_out).all(), True)


class TestIscloseDygraph(unittest.TestCase):
    def test_api_case(self):
        places = [paddle.CPUPlace()]
        if paddle.fluid.core.is_compiled_with_cuda():
            places.append(paddle.CUDAPlace(0))
        for place in places:
            paddle.disable_static()
            x_data = np.random.rand(10, 10)
            y_data = np.random.rand(10, 10)
            x = paddle.to_tensor(x_data, place=place)
            y = paddle.to_tensor(y_data, place=place)
            out = paddle.isclose(x, y, rtol=1e-05, atol=1e-08)
            expected_out = np.isclose(x_data, y_data, rtol=1e-05, atol=1e-08)
            self.assertTrue((out.numpy() == expected_out).all(), True)
        paddle.enable_static()


class TestIscloseError(unittest.TestCase):
    def test_input_dtype(self):
        paddle.enable_static()

        def test_x_dtype():
            with paddle.static.program_guard(
                paddle.static.Program(), paddle.static.Program()
            ):
                x = paddle.static.data(name='x', shape=[10, 10], dtype='int32')
                y = paddle.static.data(
                    name='y', shape=[10, 10], dtype='float64'
                )
                result = paddle.isclose(x, y)

        self.assertRaises(TypeError, test_x_dtype)

        def test_y_dtype():
            with paddle.static.program_guard(
                paddle.static.Program(), paddle.static.Program()
            ):
                x = paddle.static.data(
                    name='x', shape=[10, 10], dtype='float64'
                )
                y = paddle.static.data(name='y', shape=[10, 10], dtype='int32')
                result = paddle.isclose(x, y)

        self.assertRaises(TypeError, test_y_dtype)

    def test_attr(self):
        paddle.enable_static()
        x = paddle.static.data(name='x', shape=[10, 10], dtype='float64')
        y = paddle.static.data(name='y', shape=[10, 10], dtype='float64')

        def test_rtol():
            result = paddle.isclose(x, y, rtol=True)

        self.assertRaises(TypeError, test_rtol)

        def test_atol():
            result = paddle.isclose(x, y, rtol=True)

        self.assertRaises(TypeError, test_atol)

        def test_equal_nan():
            result = paddle.isclose(x, y, equal_nan=1)

        self.assertRaises(TypeError, test_equal_nan)


class TestIscloseOpFp16(unittest.TestCase):
    def test_fp16(self):
        x_data = np.random.rand(10, 10).astype('float16')
        y_data = np.random.rand(10, 10).astype('float16')
        with paddle.static.program_guard(paddle.static.Program()):
            x = paddle.static.data(shape=[10, 10], name='x', dtype='float16')
            y = paddle.static.data(shape=[10, 10], name='x', dtype='float16')
            out = paddle.isclose(x, y, rtol=1e-05, atol=1e-08)
            if core.is_compiled_with_cuda():
                place = paddle.CUDAPlace(0)
                exe = paddle.static.Executor(place)
                exe.run(paddle.static.default_startup_program())
                out = exe.run(feed={'x': x_data, 'y': y_data}, fetch_list=[out])


class TestIscloseOpFloat16(TestIscloseOp):
    def set_args(self):
        self.input = np.array([10.1]).astype("float16")
        self.other = np.array([10]).astype("float16")
        self.rtol = np.array([0.01]).astype("float64")
        self.atol = np.array([0]).astype("float64")
        self.equal_nan = False

    def test_check_output(self):
        if core.is_compiled_with_cuda():
            place = core.CUDAPlace(0)
            if core.is_float16_supported(place):
                self.check_output_with_place(place)


class TestIscloseOpFloat32(TestIscloseOp):
    def set_args(self):
        self.input = np.array([10.1]).astype("float32")
        self.other = np.array([10]).astype("float32")
        self.rtol = np.array([0.01]).astype("float64")
        self.atol = np.array([0]).astype("float64")
        self.equal_nan = False


class TestIscloseOpFloat64(TestIscloseOp):
    def set_args(self):
        self.input = np.array([10.1]).astype("float64")
        self.other = np.array([10]).astype("float64")
        self.rtol = np.array([0.01]).astype("float64")
        self.atol = np.array([0]).astype("float64")
        self.equal_nan = False

    def test_check_output(self):
        self.check_output()


class TestIscloseOpLargeDimInput(TestIscloseOp):
    def set_args(self):
        self.input = np.array(np.zeros([2048, 1024])).astype("float64")
        self.other = np.array(np.zeros([2048, 1024])).astype("float64")
        self.input[-1][-1] = 100
        self.rtol = np.array([1e-05]).astype("float64")
        self.atol = np.array([1e-08]).astype("float64")
        self.equal_nan = False


if __name__ == "__main__":
    paddle.enable_static()
    unittest.main()
