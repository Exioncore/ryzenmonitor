# Author: Exioncore

_pkgbase=ryzenmonitor
pkgname=ryzenmonitor
pkgver=0.1.0
pkgrel=1
pkgdesc="Linux Kernel Module to expose Ryzen 7000 telemetry"
arch=('x86_64')
url="https://github.com/Exioncore/ryzenmonitor.git"
license=('GPL3')
depends=('dkms')
makedepends=('git')
provides=('ryzenmonitor')
conflicts=('ryzenmonitor')
source=("$_pkgbase::git+$url")
sha256sums=('SKIP')

prepare() {
    sed -e "s/@VERSION@/$pkgver/" \
        -i "${srcdir}/${_pkgbase}/dkms.conf"
    echo "blacklist k10temp" > "${srcdir}/${_pkgbase}/ryzenmonitor.conf"
    echo "\nryzenmonitor" >> "${srcdir}/${_pkgbase}/ryzenmonitor.conf"
}

package() {
    install -d "${pkgdir}/usr/src/${_pkgbase}-${pkgver}/"
    install -Dm644 "${srcdir}/${_pkgbase}/dkms.conf" "$pkgdir/usr/src/${_pkgbase}-${pkgver}/."
    install -Dm644 "${srcdir}/${_pkgbase}/Makefile" "$pkgdir/usr/src/${_pkgbase}-${pkgver}/."
    install -Dm644 "${srcdir}/${_pkgbase}/ryzenmonitor.h" "$pkgdir/usr/src/${_pkgbase}-${pkgver}/."
    install -Dm644 "${srcdir}/${_pkgbase}/ryzenmonitor.c" "$pkgdir/usr/src/${_pkgbase}-${pkgver}/."
    # Enable ryzenmonitor
    install -d "${pkgdir}/etc/modules-load.d/"
    install -Dm644 "${srcdir}/${_pkgbase}/ryzenmonitor.conf" "${pkgdir}/etc/modules-load.d/."
}
