# Copyright (c) 2022 PaddlePaddle Authors. All Rights Reserved.
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

import hypothesis.strategies as st
from auto_scan_test import PassAutoScanTest
from program_config import OpConfig, ProgramConfig, TensorConfig


class TestIdentityScaleCleanPass(PassAutoScanTest):
    def sample_predictor_configs(self, program_config):
        config = self.create_inference_config(use_gpu=True)
        yield config, ['relu'], (1e-5, 1e-5)

    def sample_program_config(self, draw):
        bias_after_scale = draw(st.booleans())
        n = draw(st.integers(min_value=1, max_value=4))
        c = draw(st.integers(min_value=1, max_value=20))
        h = draw(st.integers(min_value=1, max_value=20))
        w = draw(st.integers(min_value=1, max_value=20))

        relu_op = OpConfig(
            "relu", inputs={"X": ["relu_x"]}, outputs={"Out": ["relu_out"]}
        )
        scale_op = OpConfig(
            "scale",
            inputs={"X": ["relu_out"]},
            outputs={"Out": ["scale_out"]},
            bias=0.0,
            scale=1.0,
            bias_after_scale=True,
        )
        program_config = ProgramConfig(
            ops=[relu_op, scale_op],
            weights={},
            inputs={"relu_x": TensorConfig(shape=[n, c, h, w])},
            outputs=["scale_out"],
        )
        return program_config

    def test(self):
        self.run_and_statis(max_examples=25, passes=["identity_op_clean_pass"])


if __name__ == "__main__":
    unittest.main()
