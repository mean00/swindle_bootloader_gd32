FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
ARG USER_UID=1000
ARG USER_GID=1000
ARG USERNAME=ubuntu

RUN : \
  && sed -i 's/archive.ubuntu.com/192.168.0.9:3142\/archive.ubuntu.com/g' /etc/apt/sources.list.d/ubuntu.sources \
  && sed -i 's/security.ubuntu.com/192.168.0.9:3142\/security.ubuntu.com/g' /etc/apt/sources.list.d/ubuntu.sources \
  && apt-get update \
  && apt-get install -y \
    apt-utils \
    bison \
    build-essential \
    bzip2 \
    ca-certificates \
    ccache \
    check \
    curl \
    flex \
    git \
    git-lfs \
    gperf \
    lcov \
    libbsd-dev \
    libffi-dev \
    libglib2.0-0 \
    libncurses-dev \
    libpixman-1-0 \
    libsdl2-2.0-0 \
    libslirp0 \
    libusb-1.0-0-dev \
    make \
    ninja-build \
    python3 \
    python3-venv \
    python3-crcmod \
    ruby \
    sudo \
    unzip \
    wget \
    xz-utils \
    zip \
    make cmake ninja-build usbutils \
  && apt-get autoremove -y \
  && rm -rf /var/lib/apt/lists/* \
  && update-alternatives --install /usr/bin/python python /usr/bin/python3 10 \
  && :

#
#  Reuse Arm & Riscv toolchains
#
RUN mkdir /arm && mkdir /riscv && mkdir /opt/pico && chown ubuntu /opt/pico && chown $USERNAME /riscv /arm
RUN chmod a+rwx /root

CMD [ "/bin/bash" ]

USER ${USERNAME}
ENV PATH="/home/${USERNAME}/.cargo/bin:${PATH}"
RUN  wget -nv --no-proxy -O - https://github.com/mean00/clang_arm_devcontainer/releases/download/v0.1/clang_toolchain_arm_21.1.1_x86_64_ubuntu.24_04.tar.bz2  \
  | bzip2 -dc   | tar -x -C /arm && mv /arm/ATfE-21.1.1-Linux-x86_64   /arm/llvm_21.1 \
            && cd /arm/llvm_21.1/bin && ln -s clang++ clang++-21




