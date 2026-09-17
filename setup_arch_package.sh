#!/bin/bash

if [[ "$(uname)" != "Linux" ]] || ! command -v pacman > /dev/null; then
    echo "This script can only be run on Arch-based Linux!"
    exit 1
fi


# sudo at the beginning
sudo echo > /dev/null


if [[ ! -f build/PDP11 ]]; then
    sh compile.sh || exit 1
fi


mkdir pdp11_pkg
cp build/PDP11 pdp11_pkg
cp Docs/PDP11.pdf pdp11_pkg
cp Docs/"PDP11 RU.pdf" pdp11_pkg
cp assets/icon.png pdp11_pkg/icon.png
cd pdp11_pkg


# Info about Packager
export PACKAGER="Arta <arta@gmail.com>"


echo '# Maintainer: Arta <arta@gmail.com>
pkgname=pdp11
pkgver=1.0.0
pkgrel=1
pkgdesc="Command System Emulator PDP-11"
arch=("x86_64")
url="https://github.com/Arta48/PDP11"
depends=(
    qt6-base
    gcc-libs
    glibc
    hicolor-icon-theme
)
makedepends=(
    imagemagick
)
optdepends=(
    "qt6-wayland: native Wayland support"
)
source=(
    "PDP11"
    "PDP11.pdf"
    "PDP11 RU.pdf"
    "icon.png"
)
sha256sums=(
    "SKIP"
    "SKIP"
    "SKIP"
    "SKIP"
)

prepare() {
    cd "${srcdir}"

    # Generate icons of different sizes
    sizes=("16" "24" "32" "48" "64" "128" "256")
    for size in "${sizes[@]}"; do
        magick icon.png -resize "${size}x${size}" -gravity center -background transparent -extent "${size}x${size}" "icon-${size}.png"
    done

    # Создание desktop-файла
    cat > "${pkgname}.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Command System Emulator PDP-11
Name[ru]=Эмулятор системы команд PDP-11
Exec=pdp11 %F
Icon=${pkgname}
Terminal=false
Categories=Development;Education;Emulator;
MimeType=application/x-pdp;
StartupWMClass=pdp11
Keywords=pdp;pdp11;pdp-11;emulator;assembly;asm;machine;binary;эмулятор;
EOF
}

package() {
    cd "${srcdir}"

    # Installing the binary and resources in /opt
    install -Dm755 PDP11 "${pkgdir}/opt/${pkgname}/PDP11"
    install -Dm644 PDP11.pdf "${pkgdir}/opt/${pkgname}/PDP11.pdf"
    install -Dm644 "PDP11 RU.pdf" "${pkgdir}/opt/${pkgname}/PDP11 RU.pdf"

    # Creating a symbolic link in /usr/bin
    install -d "${pkgdir}/usr/bin"
    ln -s /opt/${pkgname}/PDP11 "${pkgdir}/usr/bin/${pkgname}"

    # Installing Icons
    sizes=("16" "24" "32" "48" "64" "128" "256")
    for size in "${sizes[@]}"; do
        install -Dm644 "icon-${size}.png" "${pkgdir}/usr/share/icons/hicolor/${size}x${size}/apps/${pkgname}.png"
    done

    # Installing a desktop file
    install -Dm644 "${pkgname}.desktop" "${pkgdir}/usr/share/applications/${pkgname}.desktop"
}' > PKGBUILD


makepkg -si --skipinteg --noconfirm


cd .. && rm -rf pdp11_pkg


# Status output
echo "

Done!"
