#!/bin/bash
# Manual .deb package builder (works on macOS and Linux)

set -e

# Prevent macOS from creating ._* metadata files
export COPYFILE_DISABLE=1

PACKAGE_NAME="osoyoo-dsi-panel-dkms"
VERSION="1.0-1"
ARCH="all"
DEB_FILE="${PACKAGE_NAME}_${VERSION}_${ARCH}.deb"
BUILD_DIR="deb-build"

echo "Building ${DEB_FILE} manually..."

# Clean previous builds
rm -rf "${BUILD_DIR}"
rm -f "../${DEB_FILE}"

# Create package structure
mkdir -p "${BUILD_DIR}/DEBIAN"
mkdir -p "${BUILD_DIR}/usr/src/osoyoo-dsi-panel-1.0"

# Copy control files
cat > "${BUILD_DIR}/DEBIAN/control" << 'EOF'
Package: osoyoo-dsi-panel-dkms
Version: 1.0-1
Section: kernel
Priority: optional
Architecture: all
Depends: dkms, device-tree-compiler
Maintainer: OSOYOO <support@osoyoo.com>
Description: OSOYOO DSI Panel Driver (DKMS)
 Kernel driver for OSOYOO 720x1280 DSI touchscreen panels
 designed for Raspberry Pi. Supports 7" and 10.1" variants.
 .
 This package uses DKMS to automatically rebuild the driver
 modules when the kernel is upgraded.
EOF

# Copy postinst script
cp debian/postinst "${BUILD_DIR}/DEBIAN/postinst"
chmod 755 "${BUILD_DIR}/DEBIAN/postinst"

# Copy prerm script
cp debian/prerm "${BUILD_DIR}/DEBIAN/prerm"
chmod 755 "${BUILD_DIR}/DEBIAN/prerm"

# Copy postrm script
cp debian/postrm "${BUILD_DIR}/DEBIAN/postrm"
chmod 755 "${BUILD_DIR}/DEBIAN/postrm"

# Copy source files
cp osoyoo-panel-dsi.c "${BUILD_DIR}/usr/src/osoyoo-dsi-panel-1.0/"
cp osoyoo-panel-regulator.c "${BUILD_DIR}/usr/src/osoyoo-dsi-panel-1.0/"
cp Makefile "${BUILD_DIR}/usr/src/osoyoo-dsi-panel-1.0/"
cp dkms.conf "${BUILD_DIR}/usr/src/osoyoo-dsi-panel-1.0/"
cp osoyoo-panel-dsi-7inch.dts "${BUILD_DIR}/usr/src/osoyoo-dsi-panel-1.0/"
cp osoyoo-panel-dsi-10inch.dts "${BUILD_DIR}/usr/src/osoyoo-dsi-panel-1.0/"

# Remove any macOS metadata files
find "${BUILD_DIR}" -name "._*" -delete 2>/dev/null || true
find "${BUILD_DIR}" -name ".DS_Store" -delete 2>/dev/null || true

# Set permissions
find "${BUILD_DIR}" -type d -exec chmod 755 {} \;
find "${BUILD_DIR}" -type f -exec chmod 644 {} \;
chmod 755 "${BUILD_DIR}/DEBIAN/postinst"
chmod 755 "${BUILD_DIR}/DEBIAN/prerm"
chmod 755 "${BUILD_DIR}/DEBIAN/postrm"

# Calculate installed size (in KB)
SIZE=$(du -sk "${BUILD_DIR}" | cut -f1)
echo "Installed-Size: ${SIZE}" >> "${BUILD_DIR}/DEBIAN/control"

# Build the .deb package
echo "Creating .deb archive..."

# Check if we're on macOS or Linux
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS - use built-in tools
    if command -v dpkg-deb &> /dev/null; then
        dpkg-deb --build "${BUILD_DIR}" "../${DEB_FILE}"
    else
        echo "Warning: dpkg-deb not found. Creating tarball instead."
        echo "You'll need to convert this to .deb on a Linux system."

        # Create data and control tarballs (excluding macOS metadata)
        (cd "${BUILD_DIR}" && tar --exclude='._*' --exclude='.DS_Store' -czf ../data.tar.gz --exclude='DEBIAN' .)
        (cd "${BUILD_DIR}/DEBIAN" && tar --exclude='._*' --exclude='.DS_Store' -czf ../../control.tar.gz .)

        # Create debian-binary
        echo "2.0" > debian-binary

        # Create the .deb (which is just an ar archive)
        ar -r "../${DEB_FILE}" debian-binary control.tar.gz data.tar.gz

        # Cleanup
        rm -f debian-binary control.tar.gz data.tar.gz
    fi
else
    # Linux
    dpkg-deb --build "${BUILD_DIR}" "../${DEB_FILE}"
fi

echo ""
echo "✓ Package created: ../${DEB_FILE}"
echo ""
echo "To test on a Raspberry Pi:"
echo "  scp ../${DEB_FILE} pi@your-pi-ip:"
echo "  ssh pi@your-pi-ip"
echo "  sudo dpkg -i ${DEB_FILE}"
echo ""

# Cleanup
rm -rf "${BUILD_DIR}"
