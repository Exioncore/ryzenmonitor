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
provides=('ryzenmonitor')
conflicts=('ryzenmonitor')
source=("$_pkgname::git+$url.git")
sha256sums=('SKIP')

prepare() {
  sed -e "s/@VERSION@/$pkgver/" \
      -i "$srcdir/$_pkgname/dkms.conf"
}

package() {
  install -Dm644 "$srcdir/$_pkgname/dkms.conf" "$pkgdir/usr/src/$_pkgname-$pkgver/."
  install -Dm644 "$srcdir/$_pkgname/Makefile" "$pkgdir/usr/src/$_pkgname-$pkgver/."
  install -Dm644 "$srcdir/$_pkgname/*.h" "$pkgdir/usr/src/$_pkgname-$pkgver/."
  install -Dm644 "$srcdir/$_pkgname/*.c" "$pkgdir/usr/src/$_pkgname-$pkgver/."
}
