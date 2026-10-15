# Story of Alicia Launcher
# SPDX-FileCopyrightText: 2026 Story Of Alicia Contributors
# SPDX-License-Identifier: GPL-3.0-only
# Additional terms apply, see ADDITIONAL_TERMS.md.

# syntax=docker/dockerfile:1

FROM swift:6.3.2-jammy

ARG DEBIAN_FRONTEND=noninteractive
ARG QT_VERSION=6.8.3
ARG AQT_VERSION=3.3.0
ARG SOA_UID=1000
ARG SOA_GID=1000

RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        binutils \
        build-essential \
        ca-certificates \
        ccache \
        clang \
        cmake \
        curl \
        desktop-file-utils \
        file \
        g++-mingw-w64-i686 \
        gcc-mingw-w64-i686 \
        git \
        gnupg2 \
        jq \
        libatomic1 \
        libb2-1 \
        libcurl4-openssl-dev \
        libdbus-1-3 \
        libdouble-conversion3 \
        libedit2 \
        libegl1 \
        libfontconfig1 \
        libfreetype6 \
        libfuse2 \
        libgbm1 \
        libgcc-11-dev \
        libgl1 \
        libgl1-mesa-dev \
        libglib2.0-0 \
        libglx0 \
        libgtk-3-0 \
        libice6 \
        libicu70 \
        libinput10 \
        libmd4c0 \
        libncurses6 \
        libopengl0 \
        libpcre2-16-0 \
        libpng16-16 \
        libproxy1v5 \
        libpython3-dev \
        libsm6 \
        libsqlite3-0 \
        libssl-dev \
        libstdc++-11-dev \
        libudev1 \
        libvulkan1 \
        libwayland-client0 \
        libwayland-cursor0 \
        libwayland-dev \
        libwayland-egl1 \
        libwayland-server0 \
        libx11-6 \
        libx11-xcb1 \
        libxcb-cursor0 \
        libxcb-glx0 \
        libxcb-icccm4 \
        libxcb-image0 \
        libxcb-keysyms1 \
        libxcb-randr0 \
        libxcb-render-util0 \
        libxcb-render0 \
        libxcb-shape0 \
        libxcb-shm0 \
        libxcb-sync1 \
        libxcb-util1 \
        libxcb-xfixes0 \
        libxcb-xinerama0 \
        libxcb-xinput0 \
        libxcb-xkb1 \
        libxcb1 \
        libxext6 \
        libxkbcommon-dev \
        libxkbcommon-x11-0 \
        libxrender1 \
        libxml2-dev \
        libz3-dev \
        ninja-build \
        openssl \
        patchelf \
        pkg-config \
        python3-pip \
        squashfs-tools \
        unzip \
        wget \
        xauth \
        xvfb \
        xz-utils \
        zip \
        zlib1g-dev \
        zsync \
    && rm -rf /var/lib/apt/lists/*

RUN python3 -m pip install --no-cache-dir "aqtinstall==${AQT_VERSION}" \
    && python3 -m aqt install-qt \
        linux desktop "${QT_VERSION}" linux_gcc_64 \
        --outputdir /opt/Qt

ENV QT_ROOT_DIR=/opt/Qt/${QT_VERSION}/gcc_64 \
    QMAKE=/opt/Qt/${QT_VERSION}/gcc_64/bin/qmake \
    CMAKE_PREFIX_PATH=/opt/Qt/${QT_VERSION}/gcc_64 \
    QT_PLUGIN_PATH=/opt/Qt/${QT_VERSION}/gcc_64/plugins \
    PATH=/opt/Qt/${QT_VERSION}/gcc_64/bin:${PATH} \
    APPIMAGE_EXTRACT_AND_RUN=1

RUN test -x "${QMAKE}" \
    && test -f "${QT_ROOT_DIR}/lib/cmake/Qt6/Qt6Config.cmake" \
    && test -f "${QT_ROOT_DIR}/plugins/platforms/libqxcb.so" \
    && { test -f "${QT_ROOT_DIR}/plugins/platforms/libqwayland.so" \
         || { test -f "${QT_ROOT_DIR}/plugins/platforms/libqwayland-egl.so" \
              && test -f "${QT_ROOT_DIR}/plugins/platforms/libqwayland-generic.so"; }; } \
    && cmake --version \
    && ninja --version \
    && swiftc --version \
    && i686-w64-mingw32-gcc --version \
    && "${QMAKE}" -query QT_VERSION

RUN groupadd --gid "${SOA_GID}" soa \
    && useradd --uid "${SOA_UID}" --gid "${SOA_GID}" \
        --create-home --shell /bin/bash soa \
    && mkdir -p /workspace/soa-launcher-qt /home/soa/.cache \
    && chown -R soa:soa /workspace /home/soa

USER soa
WORKDIR /workspace/soa-launcher-qt

CMD ["/bin/bash"]
