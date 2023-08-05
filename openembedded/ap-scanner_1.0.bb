SUMMARY = "A simple WiFi scanner using libnl"
DESCRIPTION = "A very simple software that scans for WiFi Access Points using libnl."

LICENSE = "LGPL-2.1-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/${LICENSE};md5=1a6d268fd218675ffea8be556788b780"

SRC_URI = "git://github.com/r3flow/libnl-ap-scanner;branch=master;protocol=https"

SRCREV = "${AUTOREV}"
PV = "1.0+git${SRCPV}"

S = "${WORKDIR}/git"

DEPENDS = "libnl"

RDEPENDS_${PN}:append = "libnl"
RDEPENDS_${PN}:append = "libnl-genl"

inherit pkgconfig

do_compile() {
    ${CXX} -std=c++20 -Wall -g -Wfloat-conversion -Wpedantic -Wno-switch `pkg-config --cflags libnl-genl-3.0` ${CXXFLAGS} -c main.cpp
    ${CXX} `pkg-config --libs libnl-genl-3.0` ${LDFLAGS} -o ap-scanner main.o
}

do_install () {
   install -d ${D}/usr/bin
   install -D -m 755 ${S}/ap-scanner ${D}/usr/bin/
}

FILES_${PN}:append = "/usr/bin/ap-scanner"
