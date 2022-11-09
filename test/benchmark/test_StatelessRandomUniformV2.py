# Copyright (c) 2022 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the License);
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an AS IS BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ==============================================================================

import numpy as np
import tensorflow as tf
from tensorflow.python.framework import dtypes
from tensorflow.python.framework import ops
from tensorflow.python.ops import gen_stateless_random_ops
from tensorflow.python.ops import random_ops
from tensorflow.python.ops import gen_stateless_random_ops_v2
from tensorflow.python.framework import constant_op
from utils import multi_run, add_profiling, flush_cache
from utils import tailed_no_tailed_size

try:
    from intel_extension_for_tensorflow.python.test_func import test
    INT_COMPUTE_TYPE = [dtypes.int32, dtypes.int64]
except ImportError:
    from tensorflow.python.platform import test
    INT_COMPUTE_TYPE = [dtypes.int32, dtypes.int64]

ITERATION = 5

_get_key_counter_alg = (gen_stateless_random_ops_v2.stateless_random_get_key_counter_alg)
class StatelessRandomUniformTestV2(test.TestCase):
    def _test_impl(self, size, dtype):
        seed = [1, 2]
        key, counter, alg = _get_key_counter_alg(seed)
        flush_cache()
        out_gpu = gen_stateless_random_ops_v2.stateless_random_uniform_v2(shape=size, key=key, counter=counter, alg=alg, dtype=tf.dtypes.float32, name=None )

    @add_profiling
    @multi_run(ITERATION)
    def testStatelessRandomUniformV2(self):
        for dtype in INT_COMPUTE_TYPE:
            # test tailed_no_tailed_size
            for in_size in tailed_no_tailed_size:
                self._test_impl([in_size], dtype)

if __name__ == '__main__':
    test.main()  
