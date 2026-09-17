#!/bin/bash
set -e

if [[ "$(uname)" != "Linux" ]] || ! command -v pacman > /dev/null; then
    echo "This script can only be run on Arch-based Linux!"
    exit 1
fi

# Запрос sudo в самом начале
sudo echo > /dev/null

# 1. Сборка бинарника, если еще не собран
if [[ ! -f build/PDP11 ]]; then
    bash compile.sh
fi


mkdir pdp11_pkg
cp build/PDP11 pdp11_pkg
cp Docs/PDP11.pdf pdp11_pkg
cp Docs/"PDP11 RU.pdf" pdp11_pkg
cp assets/icon.png pdp11_pkg/icon.png
cd pdp11_pkg

export PACKAGER="Arta <arta@gmail.com>"

# 3. Генерация PKGBUILD
cat > PKGBUILD << 'EOF'
# Maintainer: Arta <arta@gmail.com>
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

    # Register .pdp MIME type
    cat > "${pkgname}-mime.xml" <<_MIME_EOF
<?xml version="1.0" encoding="UTF-8"?>
<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">
    <mime-type type="application/x-pdp">
        <comment>PDP-11 code files</comment>
        <comment xml:lang="ru">Файлы кода PDP-11</comment>
        <glob pattern="*.pdp"/>
        <glob pattern="*.PDP"/>
        <icon name="pdp11"/>
    </mime-type>
</mime-info>
_MIME_EOF

    # Creating a desktop file
    cat > "${pkgname}.desktop" <<_DESKTOP_EOF
[Desktop Entry]
Type=Application
Name=Command System Emulator PDP-11
Name[ru]=Эмулятор системы команд PDP-11
Exec=pdp11 %F
Icon=${pkgname}
Terminal=false
Categories=Development;Education;Emulator;
MimeType=application/x-pdp;
StartupWMClass=${pkgname}
Keywords=pdp;pdp11;pdp-11;emulator;assembly;asm;machine;binary;эмулятор;
_DESKTOP_EOF
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

    # Install MIME type
    install -Dm644 "${pkgname}-mime.xml" "${pkgdir}/usr/share/mime/packages/${pkgname}.xml"

    # Installing a desktop file
    install -Dm644 "${pkgname}.desktop" "${pkgdir}/usr/share/applications/${pkgname}.desktop"
}
EOF

# 4. Сборка и установка в систему
makepkg -si --skipinteg --noconfirm

cd .. && rm -rf pdp11_pkg

echo "
Done!
"
