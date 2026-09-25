#!/usr/bin/env bash
set -euo pipefail

PACKAGES=(
    build-essential
    make
    cmake
    pkg-config
    git
    python3

    libvulkan-dev
    vulkan-validationlayers
    vulkan-tools

    glslang-dev
    spirv-tools-dev

    libasound2-dev
    libpulse-dev
    libaudio-dev
    libjack-dev
    libsndio-dev
    libx11-dev
    libxext-dev
    libxrandr-dev
    libxcursor-dev
    libxfixes-dev
    libxi-dev
    libxss-dev
    libxtst-dev
    libxkbcommon-dev
    libdrm-dev
    libgbm-dev
    libgl1-mesa-dev
    libgles2-mesa-dev
    libegl1-mesa-dev
    libdbus-1-dev
    libibus-1.0-dev
    libudev-dev
    libusb-1.0-0-dev
    libwayland-dev
    wayland-protocols
    libdecor-0-dev
    libpipewire-0.3-dev
)

sudo apt-get update
sudo apt-get install -y "${PACKAGES[@]}"
