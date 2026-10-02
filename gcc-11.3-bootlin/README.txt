armv7-eabihf--glibc--stable-2022.08-1

This is a toolchain provided by the https://toolchains.bootlin.com/
service. This toolchain was built using Buildroot.

The complete source code for all components of the toolchain can be
found at http://toolchains.bootlin.com/downloads/releases/sources/.

The complete license text of all components of the toolchain can be
found at http://toolchains.bootlin.com/downloads/releases/licenses/.

This toolchain is based on:

    gcc                  version     11.3.0
    binutils             version       2.38
    gdb                  version       11.2
    glibc                version 2.35-134-gb6aade18a7e5719c942aa2da6cf3157aca993fa4

For those who would like to reproduce the toolchain, you can just
follow these steps:

    git clone https://github.com/bootlin/buildroot-toolchains.git buildroot
    cd buildroot
    git checkout toolchains.bootlin.com-2022.08.1

    curl http://toolchains.bootlin.com/downloads/releases/toolchains/armv7-eabihf/build_fragments/armv7-eabihf--glibc--stable-2022.08-1.defconfig > .config
    make olddefconfig
    make
    make sdk
