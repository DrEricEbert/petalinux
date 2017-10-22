#
# This file is the V4L2 capture aplication recipe.
#

SUMMARY = "V4L2 application to perform capture from V4L2 devices"
SECTION = "PETALINUX/apps"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://byteswap.h \
    file://huffman.h \
    file://v4l2grab.c \
    "

S = "${WORKDIR}"

DEPENDS = " \
	jpeg \
	"

do_compile () {
   ${CC} ${CFLAGS} ${LDFLAGS} -o v4l2-capture v4l2grab.c -ljpeg 
}

do_install() {
        install -d ${D}${bindir}
        install -m 0755 ${S}/v4l2-capture ${D}${bindir}

}

