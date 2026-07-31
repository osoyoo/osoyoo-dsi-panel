#!/bin/bash
# Direct installation script (no .deb package needed)
# This script installs the driver directly using DKMS
# Run this ON your Raspberry Pi

set -e

PACKAGE_NAME="osoyoo-dsi-panel"
PACKAGE_VERSION="1.0"
SRC_BASE="/usr/src/${PACKAGE_NAME}-${PACKAGE_VERSION}"

echo "=========================================="
echo "OSOYOO DSI Panel Driver Installation"
echo "Direct Install (No .deb package)"
echo "=========================================="
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "ERROR: This script must be run as root (use sudo)"
    exit 1
fi

# Panel size selection. Default is the 10.1" panel.
#   sudo ./install-direct.sh          -> 10.1" panel
#   sudo ./install-direct.sh 7inch    -> 7" panel
PANEL_ARG="${1:-10inch}"
case "$PANEL_ARG" in
    7|7in|7inch|7-inch)                   PANEL_SIZE="7inch" ;;
    10|10in|10inch|10-inch|10.1|10.1inch) PANEL_SIZE="10inch" ;;
    *)
        echo "Unknown panel '$PANEL_ARG' (expected 7inch or 10inch); defaulting to 10inch"
        PANEL_SIZE="10inch"
        ;;
esac

# Hardware detection function
detect_hardware() {
    local pi_model="unknown"
    local os_distro="unknown"
    local kernel_version=$(uname -r)
    local arch=$(uname -m)

    # Detect Raspberry Pi Model
    if [ -f /proc/device-tree/model ]; then
        local device_model=$(cat /proc/device-tree/model | tr -d '\0')

        case "$device_model" in
            *"Raspberry Pi 5"*|*"Raspberry Pi Compute Module 5"*)
                pi_model="pi5"
                ;;
            *"Raspberry Pi 4"*|*"Raspberry Pi Compute Module 4"*)
                pi_model="pi4"
                ;;
            *"Raspberry Pi 3"*|*"Raspberry Pi Compute Module 3"*)
                pi_model="pi3"
                ;;
            *)
                # Fallback to /proc/cpuinfo
                if grep -q "BCM2712" /proc/cpuinfo 2>/dev/null; then
                    pi_model="pi5"
                elif grep -q "BCM2711" /proc/cpuinfo 2>/dev/null; then
                    pi_model="pi4"
                elif grep -q "BCM2837" /proc/cpuinfo 2>/dev/null; then
                    pi_model="pi3"
                fi
                ;;
        esac
    fi

    # Detect OS Distribution
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        case "$ID" in
            raspbian|debian) os_distro="debian" ;;
            ubuntu) os_distro="ubuntu" ;;
            *) os_distro="$ID" ;;
        esac
    fi

    echo "$pi_model|$os_distro|$kernel_version|$arch"
}

# Install dependencies
echo "Installing dependencies..."
apt-get update
apt-get install -y dkms device-tree-compiler

# Install kernel headers based on distribution
KERNEL_VERSION=$(uname -r)
if apt-cache search raspberrypi-kernel-headers | grep -q raspberrypi-kernel-headers; then
    # Raspberry Pi OS
    echo "Detected Raspberry Pi OS - installing raspberrypi-kernel-headers..."
    apt-get install -y raspberrypi-kernel-headers
elif apt-cache search linux-headers-${KERNEL_VERSION} | grep -q linux-headers-${KERNEL_VERSION}; then
    # Standard Debian/Ubuntu
    echo "Detected Debian/Ubuntu - installing linux-headers-${KERNEL_VERSION}..."
    apt-get install -y linux-headers-${KERNEL_VERSION}
else
    echo "WARNING: Could not find kernel headers package."
    echo "         Attempting to install generic linux-headers..."
    apt-get install -y linux-headers-$(uname -r) || apt-get install -y linux-headers-generic || true
fi

echo "✓ Dependencies installed"
echo ""

# Detect hardware
echo "Detecting hardware..."
HARDWARE_INFO=$(detect_hardware)
PI_MODEL=$(echo "$HARDWARE_INFO" | cut -d'|' -f1)
OS_DISTRO=$(echo "$HARDWARE_INFO" | cut -d'|' -f2)
KERNEL_VERSION=$(echo "$HARDWARE_INFO" | cut -d'|' -f3)
ARCH=$(echo "$HARDWARE_INFO" | cut -d'|' -f4)

echo "  Raspberry Pi Model: $PI_MODEL"
echo "  OS Distribution: $OS_DISTRO"
echo "  Kernel Version: $KERNEL_VERSION"
echo "  Architecture: $ARCH"
echo ""

# Check if model-specific sources exist
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="${SCRIPT_DIR}/src/${PI_MODEL}"

if [ ! -d "$SOURCE_DIR" ]; then
    echo "WARNING: No specific driver for $PI_MODEL found."
    echo "         Checking for fallback drivers..."

    # Fallback logic
    if [ -d "${SCRIPT_DIR}/src/pi4" ]; then
        echo "         Using Pi 4 driver as fallback."
        SOURCE_DIR="${SCRIPT_DIR}/src/pi4"
    elif [ -d "${SCRIPT_DIR}/src/pi3" ]; then
        echo "         Using Pi 3 driver as fallback."
        SOURCE_DIR="${SCRIPT_DIR}/src/pi3"
    else
        echo "ERROR: No compatible driver found."
        exit 1
    fi
else
    echo "Using model-specific driver: $SOURCE_DIR"
fi
echo ""

# Remove old installation if exists
if dkms status -m ${PACKAGE_NAME} -v ${PACKAGE_VERSION} 2>/dev/null | grep -q "installed"; then
    echo "Removing previous installation..."
    dkms remove -m ${PACKAGE_NAME} -v ${PACKAGE_VERSION} --all || true
fi

# Create source directory
echo "Creating source directory..."
rm -rf "${SRC_BASE}"
mkdir -p "${SRC_BASE}"

# Copy files
echo "Copying source files..."
cp "${SCRIPT_DIR}/Makefile" "${SRC_BASE}/"
cp "${SCRIPT_DIR}/dkms.conf" "${SRC_BASE}/"
cp "$SOURCE_DIR/osoyoo-panel-dsi.c" "${SRC_BASE}/"
cp "$SOURCE_DIR/osoyoo-panel-regulator.c" "${SRC_BASE}/"
cp "$SOURCE_DIR/osoyoo-panel-dsi-7inch.dts" "${SRC_BASE}/"
cp "$SOURCE_DIR/osoyoo-panel-dsi-10inch.dts" "${SRC_BASE}/"

echo "✓ Files copied"
echo ""

# Add to DKMS
echo "Adding module to DKMS..."
dkms add -m ${PACKAGE_NAME} -v ${PACKAGE_VERSION}

# Build
echo "Building driver for kernel $KERNEL_VERSION..."
if dkms build -m ${PACKAGE_NAME} -v ${PACKAGE_VERSION}; then
    echo "✓ Build successful!"
else
    echo "ERROR: Build failed."
    echo "Make sure kernel headers are installed:"
    echo "  sudo apt-get install raspberrypi-kernel-headers"
    exit 1
fi
echo ""

# Install
echo "Installing driver module..."
if dkms install -m ${PACKAGE_NAME} -v ${PACKAGE_VERSION}; then
    echo "✓ Installation successful!"
else
    echo "ERROR: Installation failed."
    exit 1
fi
echo ""

# Install device tree overlays
echo "Installing device tree overlays..."
if [ -f "${SRC_BASE}/osoyoo-panel-dsi-7inch.dts" ]; then
    dtc -I dts -O dtb -o /tmp/osoyoo-panel-dsi-7inch.dtbo \
        "${SRC_BASE}/osoyoo-panel-dsi-7inch.dts" 2>/dev/null

    mkdir -p /boot/overlays /boot/firmware/overlays 2>/dev/null || true
    cp /tmp/osoyoo-panel-dsi-7inch.dtbo /boot/overlays/ 2>/dev/null || true
    cp /tmp/osoyoo-panel-dsi-7inch.dtbo /boot/firmware/overlays/ 2>/dev/null || true
    rm /tmp/osoyoo-panel-dsi-7inch.dtbo
    echo "  ✓ 7-inch panel overlay installed"
fi

if [ -f "${SRC_BASE}/osoyoo-panel-dsi-10inch.dts" ]; then
    dtc -I dts -O dtb -o /tmp/osoyoo-panel-dsi-10inch.dtbo \
        "${SRC_BASE}/osoyoo-panel-dsi-10inch.dts" 2>/dev/null

    mkdir -p /boot/overlays /boot/firmware/overlays 2>/dev/null || true
    cp /tmp/osoyoo-panel-dsi-10inch.dtbo /boot/overlays/ 2>/dev/null || true
    cp /tmp/osoyoo-panel-dsi-10inch.dtbo /boot/firmware/overlays/ 2>/dev/null || true
    rm /tmp/osoyoo-panel-dsi-10inch.dtbo
    echo "  ✓ 10-inch panel overlay installed"
fi

echo ""

# ---------------------------------------------------------------------------
# Configure the boot overlay automatically (idempotent, with backup).
#
# DSI lane count is chosen by Pi model:
#   Pi 3 / Pi 4  -> 2-lane  (dtoverlay=osoyoo-panel-dsi-10inch)
#   Pi 5         -> 4-lane  (dtoverlay=osoyoo-panel-dsi-10inch,4lane)
# The 10.1" overlay is 2-lane by default; the ",4lane" parameter switches it.
# The 7" panel is always 2-lane.
# ---------------------------------------------------------------------------
DSI_LANES=""
if [ "$PANEL_SIZE" = "10inch" ] && [ "$PI_MODEL" = "pi5" ]; then
    DSI_LANES=",4lane"
fi
OVERLAY_LINE="dtoverlay=osoyoo-panel-dsi-${PANEL_SIZE}${DSI_LANES}"

echo "Configuring boot overlay..."
CONFIG_TXT=/boot/firmware/config.txt
[ -f "$CONFIG_TXT" ] || CONFIG_TXT=/boot/config.txt
if [ -f "$CONFIG_TXT" ]; then
    cp -a "$CONFIG_TXT" "${CONFIG_TXT}.osoyoo.bak"
    # Remove any overlay line we added before, then append the correct one.
    sed -i '/# OSOYOO DSI panel (added by install-direct.sh)/d; /^dtoverlay=osoyoo-panel-dsi-/d' "$CONFIG_TXT"
    printf '\n# OSOYOO DSI panel (added by install-direct.sh)\n%s\n' "$OVERLAY_LINE" >> "$CONFIG_TXT"
    echo "  ✓ Updated $CONFIG_TXT"
    echo "    line:   $OVERLAY_LINE"
    echo "    backup: ${CONFIG_TXT}.osoyoo.bak"
else
    echo "  ! Could not find config.txt automatically."
    echo "    Add this line to your Pi config manually: $OVERLAY_LINE"
fi
echo ""

if [ -n "$DSI_LANES" ]; then LANE_DESC="4-lane"; else LANE_DESC="2-lane"; fi

echo "=========================================="
echo "Installation Complete!"
echo "=========================================="
echo ""
echo "Hardware Configuration:"
echo "  Model:   $PI_MODEL"
echo "  Kernel:  $KERNEL_VERSION"
echo "  Panel:   ${PANEL_SIZE} (${LANE_DESC})"
echo "  Overlay: $OVERLAY_LINE"
echo ""
echo "A reboot is required for the panel to start."
read -r -p "Reboot now? [y/N] " REBOOT_ANS || REBOOT_ANS=""
case "$REBOOT_ANS" in
    y|Y|yes|YES|Yes)
        echo "Rebooting..."
        reboot
        ;;
    *)
        echo "Reboot manually when ready:  sudo reboot"
        ;;
esac
