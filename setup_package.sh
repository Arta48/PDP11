# sudo at the beginning
sudo echo > /dev/null


if [[ ! -f build/PDP11 ]]; then
    sh compile.sh || exit 1
fi


mkdir pdp11_pkg
cp build/PDP11 pdp11_pkg
cp Docs/PDP11.pdf pdp11_pkg
cp assets/icon.png pdp11_pkg/pdp11.png
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
    "pdp11.png"
)
sha256sums=(
    "SKIP"
    "SKIP"
    "SKIP"
)

prepare() {
    cd "${srcdir}"

    # Generate icons of different sizes
    sizes=("16" "24" "32" "48" "64" "128" "256")
    for size in "${sizes[@]}"; do
        magick pdp11.png -resize "${size}x${size}" -gravity center -background transparent -extent "${size}x${size}" "icon-${size}.png"
    done

    # Создание desktop-файла
    cat > "${pkgname}.desktop" <<EOF
[Desktop Entry]
Type=Application
Categories=Development;Education;Emulator;
Name=Command System Emulator PDP-11
Exec=pdp11
Icon=pdp11
Terminal=false
StartupWMClass=pdp11
EOF
}

package() {
    cd "${srcdir}"

    # Installing the binary and resources in /opt
    install -Dm755 PDP11 "${pkgdir}/opt/pdp11/PDP11"
    install -Dm644 PDP11.pdf "${pkgdir}/opt/pdp11/PDP11.pdf"

    # Creating a symbolic link in /usr/bin
    install -d "${pkgdir}/usr/bin"
    ln -s /opt/pdp11/PDP11 "${pkgdir}/usr/bin/pdp11"

    # Installing Icons
    sizes=("16" "24" "32" "48" "64" "128" "256")
    for size in "${sizes[@]}"; do
        install -Dm644 "icon-${size}.png" "${pkgdir}/usr/share/icons/hicolor/${size}x${size}/apps/pdp11.png"
    done

    # Installing a desktop file
    install -Dm644 "${pkgname}.desktop" "${pkgdir}/usr/share/applications/${pkgname}.desktop"
}' > PKGBUILD


makepkg -si --skipinteg --noconfirm


cd .. && rm -rf pdp11_pkg


# Status output
echo "

Done!"
