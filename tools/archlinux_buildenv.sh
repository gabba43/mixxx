#!/bin/bash
# This script installs Mixxx's build dependencies on Arch Linux and derivatives.

set -euo pipefail

install_packages() {
    local packages=(
        cmake
        ninja
        pkgconf
        gcc
        make
        git
        chromaprint
        faad2
        ffmpeg
        fftw
        flac
        glib2
        glu
        hidapi
        lame
        libebur128
        libglvnd
        libid3tag
        libkeyfinder
        libmad
        libmodplug
        libogg
        libsndfile
        libusb
        libvorbis
        libx11
        libxkbcommon
        lilv
        lv2
        microsoft-gsl
        openssl
        opus
        opusfile
        pipewire
        portaudio
        portmidi
        protobuf
        qt6-5compat
        qt6-base
        qt6-declarative
        qt6-multimedia-ffmpeg
        qt6-scxml
        qt6-shadertools
        qt6-svg
        qt6-tools
        qt6-wayland
        qtkeychain-qt6
        rubberband
        soundtouch
        sqlite
        upower
        vamp-plugin-sdk
        wavpack
        zlib
    )

    if ! pacman -T taglib1 >/dev/null 2>&1 && ! pacman -T taglib >/dev/null 2>&1; then
        packages+=(taglib)
    fi

    sudo pacman -S --needed --noconfirm "${packages[@]}"
}

case "${1:-setup}" in
    name)
        echo "No build environment name required for Arch Linux." >&2
        echo "This script installs the build dependencies via pacman using the \"setup\" option." >&2
        ;;
    setup)
        install_packages
        ;;
    *)
        echo "Usage: $0 [setup|name]" >&2
        exit 1
        ;;
esac
