#!/usr/bin/env bash
#
# Copyright (c) 2021-2022 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

set -e
IMAGE_TYPE=$1
DRIVER_VERSION=${2:-}

if [ $IMAGE_TYPE == "xpu" -o $IMAGE_TYPE == "gpu" ]
then
    [ ! -z ${DRIVER_VERSION} ] && IMAGE_TYPE=${IMAGE_TYPE}-${DRIVER_VERSION//\/}
    IMAGE_NAME=intel-extension-for-tensorflow:$IMAGE_TYPE
    echo Building Image $IMAGE_NAME
    docker build --build-arg UBUNTU_VERSION=22.04 \
           --build-arg DPCPP_VER=2024.0.0-49819 \
           --build-arg MKL_VER=2024.0.0-49656 \
           --build-arg CCL_VER=2021.11.0-49156 \
           --build-arg PYTHON=python3.11 \
           --build-arg TF_VER=2.14 \
           --build-arg WHEELS=*.whl \
           --build-arg IGFX_VERSION=${DRIVER_VERSION} \
           -t $IMAGE_NAME \
	   -f docker/itex-xpu.Dockerfile .
else
        IMAGE_NAME=intel-extension-for-tensorflow:$IMAGE_TYPE
        docker build --build-arg UBUNTU_VERSION=22.04 \
                     --build-arg PYTHON=python3.11 \
                     --build-arg TF_VER=2.14 \
                     --build-arg WHEELS=*.whl \
                     -t $IMAGE_NAME \
                     -f docker/itex-cpu.Dockerfile .
fi

