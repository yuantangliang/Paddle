#   Copyright (c) 2019 PaddlePaddle Authors. All Rights Reserved.
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
"""This is unit test of Test data_norm Op."""

import unittest

import numpy as np
from eager_op_test import OpTest
from op import Operator

import paddle
from paddle import fluid
from paddle.fluid import Program, core, program_guard


def _reference_testing(x, batch_size, batch_sum, batch_square_sum, slot_dim=-1):
    x_shape = x.shape
    means_arr = batch_sum / batch_size
    scales_arr = np.sqrt(batch_size / batch_square_sum)
    min_precision = 1e-7
    if slot_dim <= 0:
        for i in range(x_shape[0]):
            x[i] -= means_arr
            x[i] *= scales_arr
        y = np.array(x)
    else:
        y = np.zeros(x_shape).astype(np.float32)
        for i in range(x_shape[0]):
            for j in range(0, x_shape[1], slot_dim):
                if x[i][j] <= -min_precision or x[i][j] >= min_precision:
                    for k in range(0, slot_dim):
                        y[i][j + k] = (
                            x[i][j + k] - means_arr[j + k]
                        ) * scales_arr[j + k]
    return y


def create_or_get_tensor(scope, var_name, var, place):
    tensor = scope.var(var_name).get_tensor()
    if var is not None:
        assert isinstance(var, np.ndarray)
        tensor.set_recursive_sequence_lengths([])
        tensor.set(var, place)
    return tensor


class TestDataNormOpInference(unittest.TestCase):
    """
    test class for data norm op
    test forward
    """

    def setUp(self):
        """
        init members of this class
        """
        self.dtype = np.float32
        self.use_mkldnn = False

    def __assert_close(self, tensor, np_array, msg, atol=1e-4):
        np.testing.assert_allclose(
            np.array(tensor), np_array, rtol=1e-05, atol=atol, err_msg=msg
        )

    def check_with_place(
        self,
        place,
        data_layout,
        dtype,
        shape,
        slot_dim=-1,
        enable_scale_and_shift=False,
    ):
        """
        do forward and check

        Args:
            place(Place): CPUPlace
            data_layout(str): NCHW or NWHC
            dtype(dtype): np.float32
            shape(list): input shape
            slot_dim(int): dimension of one slot. Refer to data_norm api.
            enable_scale_and_shift(bool): if enable scale and shift after normalization.

        """
        epsilon = 0.00001
        if len(shape) == 2:
            x_shape = shape
            c = x_shape[1]
        else:
            ValueError("len(shape) should be equal to 2")
        scale_shape = [c]

        x_val = np.random.random_sample(x_shape).astype(dtype)
        x_val = x_val - 0.5
        x_val[0][1] = 0.0
        x_val[1][1] = 0.0
        batch_size = np.ones(scale_shape).astype(np.float32)
        batch_size *= 1e4
        batch_sum = np.zeros(scale_shape).astype(np.float32)
        batch_square_sum = np.ones(scale_shape).astype(np.float32)
        batch_square_sum *= 1e4

        y_out = _reference_testing(
            x_val, batch_size, batch_sum, batch_square_sum, slot_dim
        ).astype(dtype)

        scope = core.Scope()

        # create input
        x_tensor = create_or_get_tensor(
            scope, "x_val", OpTest.np_dtype_to_fluid_dtype(x_val), place
        )
        batch_size_tensor = create_or_get_tensor(
            scope,
            "batch_size",
            OpTest.np_dtype_to_fluid_dtype(batch_size),
            place,
        )
        batch_sum_tensor = create_or_get_tensor(
            scope, "batch_sum", OpTest.np_dtype_to_fluid_dtype(batch_sum), place
        )
        batch_square_sum_tensor = create_or_get_tensor(
            scope,
            "batch_square_sum",
            OpTest.np_dtype_to_fluid_dtype(batch_square_sum),
            place,
        )

        # create output
        y_tensor = create_or_get_tensor(scope, "y_out", None, place)
        mean_tensor = create_or_get_tensor(scope, "mean", None, place)
        scales_tensor = create_or_get_tensor(scope, "scales", None, place)

        if not enable_scale_and_shift:
            data_norm_op = Operator(
                "data_norm",
                # inputs
                X="x_val",
                BatchSize="batch_size",
                BatchSum="batch_sum",
                BatchSquareSum="batch_square_sum",
                # outputs
                Y="y_out",
                Means="mean",
                Scales="scales",
                # attrs
                epsilon=epsilon,
                use_mkldnn=self.use_mkldnn,
                slot_dim=slot_dim,
                enable_scale_and_shift=False,
            )
        else:
            scale_w = np.ones(scale_shape).astype(np.float32)
            bias = np.zeros(scale_shape).astype(np.float32)
            scale_w_tensor = create_or_get_tensor(
                scope, "scale_w", OpTest.np_dtype_to_fluid_dtype(scale_w), place
            )
            bias_tensor = create_or_get_tensor(
                scope, "bias", OpTest.np_dtype_to_fluid_dtype(bias), place
            )
            data_norm_op = Operator(
                "data_norm",
                # inputs
                X="x_val",
                BatchSize="batch_size",
                BatchSum="batch_sum",
                BatchSquareSum="batch_square_sum",
                scale_w="scale_w",
                bias="bias",
                # outputs
                Y="y_out",
                Means="mean",
                Scales="scales",
                # attrs
                epsilon=epsilon,
                use_mkldnn=self.use_mkldnn,
                slot_dim=slot_dim,
                enable_scale_and_shift=True,
            )

        data_norm_op.run(scope, place)

        # check inference result
        self.__assert_close(
            y_tensor,
            y_out,
            "inference output are different at "
            + str(place)
            + ", "
            + data_layout
            + ", "
            + str(np.dtype(dtype))
            + str(np.array(y_tensor))
            + str(y_out),
            atol=1e-3,
        )

    def test_check_output(self):
        """
        test check forward, check output
        """
        places = [core.CPUPlace()]
        for place in places:
            for data_format in ["NCHW", "NHWC"]:
                for slot_dim in [-1, 1]:
                    for enable_scale_and_shift in [False, True]:
                        self.check_with_place(
                            place,
                            data_format,
                            self.dtype,
                            [2, 3],
                            slot_dim=slot_dim,
                            enable_scale_and_shift=enable_scale_and_shift,
                        )


class TestDataNormOp(OpTest):
    """
    test class for data norm op
    test forward and backward
    """

    def setUp(self):
        """
        init data norm op test env
        """
        self.op_type = 'data_norm'
        self.use_mkldnn = False
        epsilon = 0.00001
        x_shape = [10, 12]
        scale_shape = [12]
        tp = np.float32

        x_val = np.random.random(x_shape).astype(tp)
        batch_size = np.ones(scale_shape).astype(tp)
        batch_size *= 1e4
        batch_sum = np.zeros(scale_shape).astype(tp)
        batch_square_sum = np.ones(scale_shape).astype(tp)
        batch_square_sum *= 1e4

        y = np.array(x_val)

        mean = np.zeros(x_shape[1]).astype(tp)
        scale = np.ones(x_shape[1]).astype(tp)

        self.inputs = {
            "X": x_val,
            "BatchSize": batch_size,
            "BatchSum": batch_sum,
            "BatchSquareSum": batch_square_sum,
        }
        self.outputs = {"Y": y, "Means": mean, "Scales": scale}
        self.attrs = {"epsilon": epsilon, "use_mkldnn": self.use_mkldnn}

    def test_check_output(self):
        """
        test check forward, check output
        """
        # NODE(yjjiang11): This op will be deprecated.
        self.check_output(check_dygraph=False)

    def test_check_grad(self):
        """
        test check backward, check grad
        """
        # NODE(yjjiang11): This op will be deprecated.
        self.check_grad(['X'], 'Y', no_grad_set=set(), check_dygraph=False)


class TestDataNormOpWithEnableScaleAndShift(OpTest):
    """
    test class for data norm op
    test forward and backward
    """

    def setUp(self):
        """
        init data norm op test env
        """
        self.op_type = 'data_norm'
        self.use_mkldnn = False
        epsilon = 0.00001
        slot_dim = -1
        enable_scale_and_shift = True
        x_shape = [2, 50]
        scale_shape = [50]
        tp = np.float32

        x_val = np.random.uniform(-1, 1, x_shape).astype(tp)
        batch_size = np.ones(scale_shape).astype(tp)
        batch_size *= 1e4
        batch_sum = np.zeros(scale_shape).astype(tp)
        batch_square_sum = np.ones(scale_shape).astype(tp)
        batch_square_sum *= 1e4
        scale_w = np.ones(scale_shape).astype(tp)
        bias = np.zeros(scale_shape).astype(tp)

        y = np.array(x_val)

        mean = np.zeros(x_shape[1]).astype(tp)
        scale = np.ones(x_shape[1]).astype(tp)

        self.inputs = {
            "X": x_val,
            "BatchSize": batch_size,
            "BatchSum": batch_sum,
            "BatchSquareSum": batch_square_sum,
            "scale_w": scale_w,
            "bias": bias,
        }
        self.outputs = {"Y": y, "Means": mean, "Scales": scale}
        self.attrs = {
            "epsilon": epsilon,
            "use_mkldnn": self.use_mkldnn,
            "slot_dim": slot_dim,
            "enable_scale_and_shift": True,
        }

    def test_check_output(self):
        """
        test check forward, check output
        """
        # NODE(yjjiang11): This op will be deprecated.
        self.check_output(check_dygraph=False)

    def test_check_grad(self):
        """
        test check backward, check grad
        """
        # NODE(yjjiang11): This op will be deprecated.
        self.check_grad(['X'], 'Y', no_grad_set=set(), check_dygraph=False)


class TestDataNormOpWithoutEnableScaleAndShift(OpTest):
    """
    test class for data norm op
    test forward and backward
    """

    def setUp(self):
        """
        init data norm op test env
        """
        self.op_type = 'data_norm'
        self.use_mkldnn = False
        epsilon = 0.00001
        slot_dim = -1
        enable_scale_and_shift = True
        x_shape = [2, 50]
        scale_shape = [50]
        tp = np.float32

        x_val = np.random.uniform(-1, 1, x_shape).astype(tp)
        batch_size = np.ones(scale_shape).astype(tp)
        batch_size *= 1e4
        batch_sum = np.zeros(scale_shape).astype(tp)
        batch_square_sum = np.ones(scale_shape).astype(tp)
        batch_square_sum *= 1e4
        scale_w = np.ones(scale_shape).astype(tp)
        bias = np.zeros(scale_shape).astype(tp)

        y = np.array(x_val)

        mean = np.zeros(x_shape[1]).astype(tp)
        scale = np.ones(x_shape[1]).astype(tp)

        self.inputs = {
            "X": x_val,
            "BatchSize": batch_size,
            "BatchSum": batch_sum,
            "BatchSquareSum": batch_square_sum,
            "scale_w": scale_w,
            "bias": bias,
        }
        self.outputs = {"Y": y, "Means": mean, "Scales": scale}
        self.attrs = {"epsilon": epsilon, "use_mkldnn": self.use_mkldnn}

    def test_check_output(self):
        """
        test check forward, check output
        """
        # NODE(yjjiang11): This op will be deprecated.
        self.check_output(check_dygraph=False)

    def test_check_grad(self):
        """
        test check backward, check grad
        """
        # NODE(yjjiang11): This op will be deprecated.
        self.check_grad(['X'], 'Y', no_grad_set=set(), check_dygraph=False)


class TestDataNormOpWithEnableScaleAndShift_1(OpTest):
    """
    test class for data norm op
    test forward and backward
    """

    def setUp(self):
        """
        init data norm op test env
        """
        self.op_type = 'data_norm'
        self.use_mkldnn = False
        epsilon = 0.00001
        slot_dim = 1
        enable_scale_and_shift = True
        x_shape = [2, 50]
        scale_shape = [50]
        tp = np.float32

        x_val = np.random.uniform(-1, 1, x_shape).astype(tp)
        batch_size = np.ones(scale_shape).astype(tp)
        batch_size *= 1e4
        batch_sum = np.zeros(scale_shape).astype(tp)
        batch_square_sum = np.ones(scale_shape).astype(tp)
        batch_square_sum *= 1e4
        scale_w = np.ones(scale_shape).astype(tp)
        bias = np.zeros(scale_shape).astype(tp)

        y = np.array(x_val)

        mean = np.zeros(x_shape[1]).astype(tp)
        scale = np.ones(x_shape[1]).astype(tp)

        self.inputs = {
            "X": x_val,
            "BatchSize": batch_size,
            "BatchSum": batch_sum,
            "BatchSquareSum": batch_square_sum,
            "scale_w": scale_w,
            "bias": bias,
        }
        self.outputs = {"Y": y, "Means": mean, "Scales": scale}
        self.attrs = {
            "epsilon": epsilon,
            "use_mkldnn": self.use_mkldnn,
            "slot_dim": slot_dim,
            "enable_scale_and_shift": True,
        }

    def test_check_output(self):
        """
        test check forward, check output
        """
        # NODE(yjjiang11): This op will be deprecated.
        self.check_output(check_dygraph=False)

    def test_check_grad(self):
        """
        test check backward, check grad
        """
        # NODE(yjjiang11): This op will be deprecated.
        self.check_grad(['X'], 'Y', no_grad_set=set(), check_dygraph=False)


class TestDataNormOpWithSlotDim(OpTest):
    """
    test class for data norm op
    test forward and backward
    """

    def setUp(self):
        """
        init data norm op test env
        """
        self.op_type = 'data_norm'
        self.use_mkldnn = False
        epsilon = 0.00001
        slot_dim = 1
        x_shape = [2, 50]
        scale_shape = [50]
        tp = np.float32

        x_val = np.random.uniform(-1, 1, x_shape).astype(tp)
        batch_size = np.ones(scale_shape).astype(tp)
        batch_size *= 1e4
        batch_sum = np.zeros(scale_shape).astype(tp)
        batch_square_sum = np.ones(scale_shape).astype(tp)
        batch_square_sum *= 1e4

        y = np.array(x_val)

        mean = np.zeros(x_shape[1]).astype(tp)
        scale = np.ones(x_shape[1]).astype(tp)

        self.inputs = {
            "X": x_val,
            "BatchSize": batch_size,
            "BatchSum": batch_sum,
            "BatchSquareSum": batch_square_sum,
        }
        self.outputs = {"Y": y, "Means": mean, "Scales": scale}
        self.attrs = {
            "epsilon": epsilon,
            "use_mkldnn": self.use_mkldnn,
            "slot_dim": slot_dim,
        }

    def test_check_output(self):
        """
        test check forward, check output
        """
        # NODE(yjjiang11): This op will be deprecated.
        self.check_output(check_dygraph=False)

    def test_check_grad(self):
        """
        test check backward, check grad
        """
        # NODE(yjjiang11): This op will be deprecated.
        self.check_grad(['X'], 'Y', no_grad_set=set(), check_dygraph=False)


class TestDataNormOpErrorr(unittest.TestCase):
    def test_errors(self):
        with program_guard(Program(), Program()):
            x2 = paddle.static.data(name='x2', shape=[-1, 3, 4], dtype="int32")
            # self.assertRaises(TypeError, fluid.data_norm, x2)
            paddle.static.nn.data_norm(
                input=x2, param_attr={}, enable_scale_and_shift=True
            )

            # Test input with dimension 1
            paddle.enable_static()
            x3 = paddle.static.data("", shape=[0], dtype="float32")
            self.assertRaises(ValueError, paddle.static.nn.data_norm, x3)

            # The size of input in data_norm should not be 0.
            def test_0_size():
                paddle.enable_static()
                x = paddle.static.data(name='x', shape=[0, 3], dtype='float32')
                out = paddle.static.nn.data_norm(x, slot_dim=1)
                cpu = fluid.core.CPUPlace()
                exe = fluid.Executor(cpu)
                exe.run(fluid.default_startup_program())
                test_program = fluid.default_main_program().clone(for_test=True)
                exe.run(
                    test_program,
                    fetch_list=out,
                    feed={'x': np.ones([0, 3]).astype('float32')},
                )

            self.assertRaises(ValueError, test_0_size)


if __name__ == '__main__':
    unittest.main()
