#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.1.10
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]; then
    echo "Using default directory ${OUTDIR} for output"
else
    OUTDIR=$1
    echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
    echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
    git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # Add your kernel build steps here
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
    make -j$(nproc) ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}  all
    # make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} modules
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs
fi

echo "Adding the Image in outdir"
cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]; then
    echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm -rf ${OUTDIR}/rootfs
fi

#Create necessary base directories
mkdir -p "${OUTDIR}/rootfs"
mkdir -p "${OUTDIR}/rootfs/home"
mkdir -p "${OUTDIR}/rootfs/etc"
mkdir -p "${OUTDIR}/rootfs/dev"
mkdir -p "${OUTDIR}/rootfs/proc"
mkdir -p "${OUTDIR}/rootfs/sys"
mkdir -p "${OUTDIR}/rootfs/tmp"
mkdir -p "${OUTDIR}/rootfs/var/log"
mkdir -p "${OUTDIR}/rootfs/usr/bin"
mkdir -p "${OUTDIR}/rootfs/lib" 
mkdir -p "${OUTDIR}/rootfs/lib64"


cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]; then
    git clone git://busybox.net/busybox.git --depth 1 --single-branch --branch ${BUSYBOX_VERSION}
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox
    make defconfig
else
    cd busybox
fi

# Make and install busybox
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} distclean
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
make -j$(nproc) ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} 
make CONFIG_PREFIX="${OUTDIR}/rootfs" ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}  install

echo "Library dependencies"
echo $(${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter")
echo $(${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library")


CROSS_COMPILE_DIR=$(${CROSS_COMPILE}gcc -print-sysroot)
# echo $(${CROSS_COMPILE}readelf -a bin/busybox | grep -oP 'Requesting program interpreter: \[\K[^]]+' | xargs -I {} cp -v ${CROSS_COMPILE_DIR}/{} ${OUTDIR}/rootfs/lib/)
# echo $(${CROSS_COMPILE}readelf -a  bin/busybox | grep -oP 'Shared library: \[\K[^]]+' | xargs -I {} cp -v ${CROSS_COMPILE_DIR}/lib64/{} ${OUTDIR}/rootfs/lib64/)

cp -v ${CROSS_COMPILE_DIR}/lib/ld-linux-aarch64.so.1 ${OUTDIR}/rootfs/lib/
cp -v ${CROSS_COMPILE_DIR}/lib64/libm.so.6 ${OUTDIR}/rootfs/lib64/
cp -v ${CROSS_COMPILE_DIR}/lib64/libresolv.so.2 ${OUTDIR}/rootfs/lib64/
cp -v ${CROSS_COMPILE_DIR}/lib64/libc.so.6  ${OUTDIR}/rootfs/lib64/

# Make device nodes
sudo mknod -m 666 ${OUTDIR}/rootfs/dev/null c 1 3
sudo mknod -m 666 ${OUTDIR}/rootfs/dev/console c 5 1


# Assuming the writer utility is in the current working directory
make clean
make CROSS_COMPILE=${CROSS_COMPILE} -C "${FINDER_APP_DIR}"

# Copy the compiled writer application to the home directory within rootfs
cp -v "${FINDER_APP_DIR}/writer" "${OUTDIR}/rootfs/home/"

# Copy finder scripts and configuration files to the home directory within rootfs
cp -v "${FINDER_APP_DIR}/finder.sh" "${OUTDIR}/rootfs/home/"
mkdir -p "${OUTDIR}/rootfs/home/conf"
cp -v "${FINDER_APP_DIR}/conf/username.txt" "${OUTDIR}/rootfs/home/conf/"
cp -v "${FINDER_APP_DIR}/conf/assignment.txt" "${OUTDIR}/rootfs/home/conf/"
cp -v "${FINDER_APP_DIR}/finder-test.sh" "${OUTDIR}/rootfs/home/"

AUTORUN_SCRIPT="${FINDER_APP_DIR}/autorun-qemu.sh"

# Copy autorun-qemu.sh script to the home directory within rootfs
cp -v "${AUTORUN_SCRIPT}" "${OUTDIR}/rootfs/home/"

#Chown the root directory
sudo chown -R root:root "${OUTDIR}/rootfs"

# Create initramfs.cpio.gz
(cd "${OUTDIR}/rootfs" && find . | cpio -o -H newc --owner root:root | gzip -f -9 >"${OUTDIR}/initramfs.cpio.gz")
