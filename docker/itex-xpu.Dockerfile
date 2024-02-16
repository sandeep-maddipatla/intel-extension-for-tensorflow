# Copyright (c) 2022-2023 Intel Corporation
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
# ============================================================================

ARG UBUNTU_VERSION

FROM ubuntu:${UBUNTU_VERSION}

ARG DEBIAN_FRONTEND=noninteractive

HEALTHCHECK NONE
RUN useradd -d /home/itex -m -s /bin/bash itex

RUN ln -sf bash /bin/sh

RUN apt-get update && \
    apt-get install -y --no-install-recommends --fix-missing \
    apt-utils \
    ca-certificates \
    clinfo \
    git \
    gnupg2 \
    gpg-agent \
    rsync \
    sudo \
    unzip \
    libnss3-tools \
    wget && \
    apt-get clean && \
    rm -rf  /var/lib/apt/lists/*

# Install certs for internal version of components
COPY assets/embargo/setup-certs.sh /tmp/
RUN /tmp/setup-certs.sh && rm -rf /tmp/setup-certs.sh

RUN wget -qO - https://repositories.gfxs.intel.com/repositories/intel-graphics.key | \
    gpg --dearmor --output /usr/share/keyrings/intel-graphics.gpg

# Variables to define Intel-Internal Gfx components to use
ARG IGFX_REPO_URL=https://repositories.gfxs.intel.com/repositories/ubuntu
ARG IGFX_VERSION=
ARG IGFX_COMPONENT=unified

RUN local_key=/usr/share/keyrings/intel-graphics.gpg && \
    igfx_path=jammy && \
    if [ ! -z $IGFX_VERSION ]; then igfx_path=$igfx_path/$IGFX_VERSION; fi && \
    source_string=$(echo $IGFX_REPO_URL $igfx_path $IGFX_COMPONENT) && \
    echo "deb [arch=amd64 signed-by=$(echo $local_key)]  $source_string" | \
    tee /etc/apt/sources.list.d/intel-gpu-jammy.list

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    intel-opencl-icd \
    intel-level-zero-gpu \
    level-zero \
    level-zero-dev

RUN sudo rm -f /opt/DriverPackages.log && \
    echo "IGFX_REPO_URL=$IGFX_REPO_URL" >> /opt/DriverPackages.log && \
    echo "IGFX_VERSION=$IGFX_VERSION" >> /opt/DriverPackages.log && \
    echo "IGFX_COMPONENT=$IGFX_COMPONENT" >> /opt/DriverPackages.log && \
    apt list -a intel-opencl-icd | tee -a /opt/DriverPackages.log && \
    apt list -a intel-level-zero-gpu | tee -a /opt/DriverPackages.log && \
    apt list -a level-zero | tee -a /opt/DriverPackages.log && \
    apt list -a level-zero-dev | tee -a /opt/DriverPackages.log  && \
    apt-get clean && \
    rm -rf  /var/lib/apt/lists/*

RUN wget -O- https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB \
   | gpg --dearmor | tee /usr/share/keyrings/oneapi-archive-keyring.gpg > /dev/null && \
   echo "deb [signed-by=/usr/share/keyrings/oneapi-archive-keyring.gpg] https://apt.repos.intel.com/oneapi all main" \
   | tee /etc/apt/sources.list.d/oneAPI.list

ARG DPCPP_VER
ARG MKL_VER
ARG CCL_VER

RUN apt-get update && \
    apt-get install -y --no-install-recommends --fix-missing \
    intel-oneapi-runtime-dpcpp-cpp=${DPCPP_VER} \
    intel-oneapi-runtime-mkl=${MKL_VER} \
    intel-oneapi-runtime-ccl=${CCL_VER} && \
    apt-get clean && \
    rm -rf  /var/lib/apt/lists/*

RUN echo "intelpython=exclude" > $HOME/cfg.txt

ENV LANG=C.UTF-8
ARG PYTHON=python3.10

RUN apt-get update && apt-get install -y --no-install-recommends --fix-missing \
    ${PYTHON} lib${PYTHON} python3-pip && \
    apt-get clean && \
    rm -rf  /var/lib/apt/lists/*

RUN pip --no-cache-dir install --upgrade \
    pip \
    setuptools

RUN ln -sf $(which ${PYTHON}) /usr/local/bin/python && \
    ln -sf $(which ${PYTHON}) /usr/local/bin/python3 && \
    ln -sf $(which ${PYTHON}) /usr/bin/python && \
    ln -sf $(which ${PYTHON}) /usr/bin/python3

ARG TF_VER="2.14"

RUN pip --no-cache-dir install tensorflow==${TF_VER}

ARG WHEELS

COPY models/binaries/$WHEELS /tmp/whls/

RUN pip install /tmp/whls/* && \
    rm -rf /tmp/whls

RUN wget -P /licenses https://raw.githubusercontent.com/intel/intel-extension-for-tensorflow/master/third-party-programs/dockerlayer/THIRD-PARTY-PROGRAMS.txt && \
    wget -P /licenses https://raw.githubusercontent.com/intel/intel-extension-for-tensorflow/master/third-party-programs/dockerlayer/dpcpp-third-party-programs.txt && \
    wget -P /licenses https://raw.githubusercontent.com/intel/intel-extension-for-tensorflow/master/third-party-programs/dockerlayer/oneccl-third-party-programs.txt && \
    wget -P /licenses https://raw.githubusercontent.com/intel/intel-extension-for-tensorflow/master/third-party-programs/dockerlayer/onemkl-third-party-program-sub-tpp.txt && \
    wget -P /licenses https://raw.githubusercontent.com/intel/intel-extension-for-tensorflow/master/third-party-programs/dockerlayer/onemkl-third-party-program.txt && \
    wget -P /licenses https://raw.githubusercontent.com/intel/intel-extension-for-tensorflow/master/third-party-programs/dockerlayer/third-party-program-of-intel-extension-for-tensorflow.txt && \
    wget -P /licenses https://raw.githubusercontent.com/intel/intel-extension-for-tensorflow/master/third-party-programs/dockerlayer/third-party-programs-of-intel-tensorflow.txt && \
    wget -P /licenses https://raw.githubusercontent.com/intel/intel-extension-for-tensorflow/master/third-party-programs/dockerlayer/third-party-programs-of-intel-optimization-for-horovod.txt && \
    wget -P /licenses https://raw.githubusercontent.com/oneapi-src/oneCCL/master/third-party-programs.txt
