-----BEGIN PGP SIGNED MESSAGE-----
Hash: SHA512

Format: 3.0 (native)
Source: mesa
Binary: libxatracker2, libxatracker-dev, libd3dadapter9-mesa, libd3dadapter9-mesa-dev, libgbm1, libgbm-dev, libegl-mesa0, libegl1-mesa, libegl1-mesa-dev, libwayland-egl1-mesa, libgles2-mesa, libgles2-mesa-dev, libglapi-mesa, libglx-mesa0, libgl1-mesa-glx, libgl1-mesa-dri, libgl1-mesa-dev, mesa-common-dev, libosmesa6, libosmesa6-dev, mesa-va-drivers, mesa-vdpau-drivers, mesa-vulkan-drivers, mesa-opencl-icd
Architecture: any
Version: 1:23.0.5-0ubuntu1~panfork~git221210.120202c6757~j3
Maintainer: First Mate Rummey <fmrummey@gmail.com>
Uploaders: Andreas Boll <aboll@debian.org>
Homepage: https://mesa3d.org/
Standards-Version: 4.1.4
Vcs-Browser: https://salsa.debian.org/xorg-team/lib/mesa
Vcs-Git: https://salsa.debian.org/xorg-team/lib/mesa.git
Build-Depends: debhelper-compat (= 12), directx-headers-dev [linux-amd64 linux-arm64], glslang-tools [amd64 arm64 armel armhf i386 mips64el mipsel powerpc ppc64 ppc64el riscv64 s390x sparc64 x32], meson (>= 0.45), quilt (>= 0.63-8.2~), pkg-config, libdrm-dev (>= 2.4.107-4), libx11-dev, libxxf86vm-dev, libexpat1-dev, libsensors-dev [!hurd-any], libxfixes-dev, libxext-dev, libva-dev (>= 1.6.0) [linux-any kfreebsd-any] <!pkg.mesa.nolibva>, libvdpau-dev (>= 1.1.1) [linux-any kfreebsd-any], libvulkan-dev [amd64 arm64 armel armhf i386 mips64el mipsel powerpc ppc64 ppc64el riscv64 s390x sparc64 x32], x11proto-dev, linux-libc-dev (>= 2.6.31) [linux-any], libx11-xcb-dev, libxcb-dri2-0-dev (>= 1.8), libxcb-glx0-dev (>= 1.8.1), libxcb-xfixes0-dev, libxcb-dri3-dev, libxcb-present-dev, libxcb-randr0-dev, libxcb-shm0-dev, libxcb-sync-dev, libxrandr-dev, libxshmfence-dev (>= 1.1), libzstd-dev, python3, python3-mako, python3-setuptools, flex, bison, libelf-dev [amd64 arm64 armel armhf i386 kfreebsd-amd64 kfreebsd-i386 mips64el mipsel powerpc ppc64 ppc64el riscv64 s390x sparc64], libwayland-dev (>= 1.15.0) [linux-any], libwayland-egl-backend-dev (>= 1.15.0) [linux-any], llvm-14-dev [amd64 arm64 armel armhf i386 mips64el mipsel powerpc ppc64 ppc64el s390x sparc64], libclang-14-dev [amd64 arm64 armel armhf i386 mips64el mipsel powerpc ppc64 ppc64el s390x sparc64], libclang-cpp14-dev [amd64 arm64 armel armhf i386 mips64el mipsel powerpc ppc64 ppc64el s390x sparc64], libclc-14-dev [amd64 arm64 armel armhf i386 mips64el mipsel powerpc ppc64 ppc64el s390x sparc64], wayland-protocols (>= 1.9), zlib1g-dev, libglvnd-core-dev (>= 1.3.2), valgrind [amd64 arm64 armhf i386 mips64el mipsel powerpc ppc64 ppc64el s390x]
Package-List:
 libd3dadapter9-mesa deb libs optional arch=amd64,arm64,armel,armhf,i386,powerpc
 libd3dadapter9-mesa-dev deb libdevel optional arch=amd64,arm64,armel,armhf,i386,powerpc
 libegl-mesa0 deb libs optional arch=any
 libegl1-mesa deb oldlibs optional arch=any
 libegl1-mesa-dev deb libdevel optional arch=any
 libgbm-dev deb libdevel optional arch=linux-any,kfreebsd-any
 libgbm1 deb libs optional arch=linux-any,kfreebsd-any
 libgl1-mesa-dev deb oldlibs optional arch=any
 libgl1-mesa-dri deb libs optional arch=any
 libgl1-mesa-glx deb oldlibs optional arch=any
 libglapi-mesa deb libs optional arch=any
 libgles2-mesa deb oldlibs optional arch=any
 libgles2-mesa-dev deb oldlibs optional arch=any
 libglx-mesa0 deb libs optional arch=any
 libosmesa6 deb libs optional arch=any
 libosmesa6-dev deb libdevel optional arch=any
 libwayland-egl1-mesa deb oldlibs optional arch=linux-any
 libxatracker-dev deb libdevel optional arch=amd64,i386,x32
 libxatracker2 deb libs optional arch=amd64,i386,x32
 mesa-common-dev deb libdevel optional arch=any
 mesa-opencl-icd deb libs optional arch=amd64,arm64,armel,armhf,i386,mips64el,mipsel,powerpc,ppc64,ppc64el,s390x,sparc64
 mesa-va-drivers deb libs optional arch=linux-any,kfreebsd-any profile=!pkg.mesa.nolibva
 mesa-vdpau-drivers deb libs optional arch=linux-any,kfreebsd-any
 mesa-vulkan-drivers deb libs optional arch=amd64,arm64,armel,armhf,i386,mips64el,mipsel,powerpc,ppc64,ppc64el,s390x,sparc64,x32
Checksums-Sha1:
 edc4e1d476cbfabc72f899e206a62034e3cd98f5 17352840 mesa_23.0.5-0ubuntu1~panfork~git221210.120202c6757~j3.tar.xz
Checksums-Sha256:
 a5080946e7a743ba5a5686c3e68b576fdcee0360852b63724b6e4972c085b17a 17352840 mesa_23.0.5-0ubuntu1~panfork~git221210.120202c6757~j3.tar.xz
Files:
 00a3091a3a742bc78789864d77c8b08e 17352840 mesa_23.0.5-0ubuntu1~panfork~git221210.120202c6757~j3.tar.xz
Original-Maintainer: Ubuntu X-SWAT <ubuntu-x@lists.ubuntu.com>

-----BEGIN PGP SIGNATURE-----

iQHOBAEBCgA4FiEEmy4EJ9eTGMZqXK8pF/4Drha1nvwFAmVdwcYaHGxpdWppYW5m
ZW5nMTk5NEBnbWFpbC5jb20ACgkQF/4Drha1nvy8Qwv6Av6x63X9mHuDDc2szoWk
8I9HnF3uutCl0r4M1t15SqLpAhqAo9R5eOZhhq+faYSln069LP1/RC35o8pCOPUF
srgvt07UdkO2//LhIC54iwklrS5pITrbykO8C+iEhGMLWVbpT99i+OXsNO+0RZfc
JS62sHdZ1pmNTEG8KvUoYyVUaDJGcbIfMwwdDp1/heMDEPRzMn90UDtioXBrdjab
wH+fHRf5YEYaNgY57RnnhJKqcXf5UqUCMPsXsH88MIxg971BYlPtf4gQBxe40yGY
kY2LJI/k7Mmb1WRcAoDq82Y3me72nuGGztg4ikHaSJ8YygQej4T4Ter5XdelRcnn
asmGgYvvmKYois11vISZMxiws6d05VbIb5h7VsttbK0z74YBAzf8HiXo6sAb3z1o
dg5pOf9Y2O1+SnXpxNFIf8PfCt4DFZv/TSJ9wGLONWBLsns6WFM2RjNLo8XlZORV
l/LYybuzHEkSgQP4SFnEiQYT9GpmUvl49zKNZqEt3zoM
=EV3K
-----END PGP SIGNATURE-----
