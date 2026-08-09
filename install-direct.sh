#!/usr/bin/env bash
#
# install-direct.sh — build + install the OSOYOO unified MIPI-DSI panel driver
#
# Usage:
#   ./install-direct.sh <screen_name>
#
#   screen_name is one of:
#     display10inch    10.1"  (800x1280)  osoyoo-panel-dsi-10inch
#     display7inch     7"     (720x1280)  osoyoo-panel-dsi-7inch
#     display3.5inch   3.5"   (480x800)   osoyoo-panel-st7701s-3p5inch
#
# What it does, automatically:
#   1) Detects the running kernel and builds the modules against ITS headers
#      (installs the headers package first if it is missing).
#   2) Picks the right .dts / .dtbo file names for the chosen screen.
#   3) Installs the modules, builds the overlay into the boot overlays dir,
#      and sets the matching  dtoverlay=...  line in config.txt (idempotently).
#
# Run it as your NORMAL user (it uses sudo only where root is required).

set -euo pipefail

# --------------------------------------------------------------------------
# 0. Locate ourselves + the source tree
# --------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

die() { echo "ERROR: $*" >&2; exit 1; }
note() { echo ">> $*"; }

usage() {
  cat >&2 <<EOF
Usage: $(basename "$0") <screen_name>

  screen_name:
    display10inch     10.1"  panel
    display7inch      7"     panel
    display3.5inch    3.5"   panel
EOF
  exit 1
}

[ $# -eq 1 ] || usage

# --------------------------------------------------------------------------
# 1. Map screen_name -> overlay base name (the .dts/.dtbo share this stem)
# --------------------------------------------------------------------------
case "$1" in
  display10inch|display10|10inch|10.1inch)
      PANEL="osoyoo-panel-dsi-10inch";        LABEL="10.1\"" ;;
  display7inch|display7|7inch)
      PANEL="osoyoo-panel-dsi-7inch";         LABEL="7\"" ;;
  display3.5inch|display3p5inch|3.5inch|3p5inch)
      PANEL="osoyoo-panel-st7701s-3p5inch";   LABEL="3.5\"" ;;
  *)
      echo "Unknown screen_name: '$1'" >&2
      usage ;;
esac

DTS_FILE="$SCRIPT_DIR/$PANEL.dts"
[ -f "$DTS_FILE" ] || die "device-tree source not found: $DTS_FILE"
[ -f "$SCRIPT_DIR/Makefile" ] || die "Makefile not found in $SCRIPT_DIR — run this from the driver folder."

# --------------------------------------------------------------------------
# 2. Detect boot layout (Trixie/Bookworm use /boot/firmware, older use /boot)
# --------------------------------------------------------------------------
if [ -d /boot/firmware/overlays ]; then
  BOOT_DIR="/boot/firmware"
elif [ -d /boot/overlays ]; then
  BOOT_DIR="/boot"
else
  die "Could not find an overlays dir (/boot/firmware/overlays or /boot/overlays)."
fi
OVERLAYS_DIR="$BOOT_DIR/overlays"
CONFIG_TXT="$BOOT_DIR/config.txt"
[ -f "$CONFIG_TXT" ] || die "config.txt not found at $CONFIG_TXT"

# --------------------------------------------------------------------------
# 3. Decide the dtoverlay= line. For the 10.1" panel the lane count depends
#    on the board: Pi 5 / CM5 use 4 lanes, Pi 3/4 use 2 (the overlay default).
# --------------------------------------------------------------------------
OVERLAY_LINE="dtoverlay=$PANEL"
MODEL="$(tr -d '\0' < /proc/device-tree/model 2>/dev/null || true)"
if [ "$PANEL" = "osoyoo-panel-dsi-10inch" ]; then
  case "$MODEL" in
    *"Raspberry Pi 5"*|*"Compute Module 5"*)
        OVERLAY_LINE="dtoverlay=$PANEL,4lane"
        note "Board is '$MODEL' -> using 4-lane variant" ;;
    *)  note "Board is '${MODEL:-unknown}' -> using 2-lane (default)" ;;
  esac
fi

# --------------------------------------------------------------------------
# 4. Detect kernel + ensure matching headers are present
# --------------------------------------------------------------------------
KREL="$(uname -r)"
KBUILD="/lib/modules/$KREL/build"
note "Running kernel: $KREL"

if [ ! -d "$KBUILD" ]; then
  note "Kernel headers for $KREL not found — installing…"
  sudo apt-get update
  sudo apt-get install -y "linux-headers-$KREL" \
    || sudo apt-get install -y linux-headers-rpi-v8 linux-headers-rpi-2712 \
    || sudo apt-get install -y raspberrypi-kernel-headers \
    || die "Could not install kernel headers for $KREL."
fi
[ -d "$KBUILD" ] || die "Kernel headers still missing at $KBUILD after install attempt."

if [ "$(id -u)" -eq 0 ]; then
  note "WARNING: running as root — building modules as root leaves root-owned files."
fi

# --------------------------------------------------------------------------
# 5. Build the modules against the detected kernel (Step 1)
#    KDIR=... overrides the Makefile's value so we always target THIS kernel.
# --------------------------------------------------------------------------
note "Building modules (make KDIR=$KBUILD)…"
make clean
make KDIR="$KBUILD"

for ko in osoyoo-dsi-panel.ko osoyoo-panel-regulator.ko; do
  [ -s "$SCRIPT_DIR/$ko" ] || die "build did not produce $ko"
done

# --------------------------------------------------------------------------
# 6. Install the modules (Step 2)
# --------------------------------------------------------------------------
note "Installing modules into /lib/modules/$KREL/ …"
sudo cp "$SCRIPT_DIR/osoyoo-dsi-panel.ko" "$SCRIPT_DIR/osoyoo-panel-regulator.ko" "/lib/modules/$KREL/"
sudo depmod "$KREL"

# --------------------------------------------------------------------------
# 7. Build the overlay straight into the boot overlays dir (Step 3)
#    (writing there with sudo avoids the root-owned-.dtbo "Permission denied"
#     trap; dtc prints harmless Warnings — only a FATAL ERROR is a failure.)
# --------------------------------------------------------------------------
note "Building overlay -> $OVERLAYS_DIR/$PANEL.dtbo …"
sudo dtc -@ -I dts -O dtb -o "$OVERLAYS_DIR/$PANEL.dtbo" "$DTS_FILE" 2>/tmp/osoyoo-dtc.log || {
  cat /tmp/osoyoo-dtc.log >&2
  die "dtc failed (see FATAL ERROR above)."
}
[ -s "$OVERLAYS_DIR/$PANEL.dtbo" ] || die "overlay .dtbo is missing or 0 bytes."
note "Overlay built OK (dtc warnings, if any, are harmless)."

# --------------------------------------------------------------------------
# 8. Set the dtoverlay= line in config.txt (Step 4), idempotently
#    Remove any previous osoyoo overlay line first, then append the new one.
# --------------------------------------------------------------------------
note "Updating $CONFIG_TXT -> '$OVERLAY_LINE'"
sudo sed -i '/^[[:space:]]*dtoverlay=osoyoo/d' "$CONFIG_TXT"
echo "$OVERLAY_LINE" | sudo tee -a "$CONFIG_TXT" >/dev/null

# --------------------------------------------------------------------------
# 9. Done — summarise + offer reboot
# --------------------------------------------------------------------------
cat <<EOF

============================================================
 OSOYOO $LABEL panel driver installed.
   kernel        : $KREL
   modules       : /lib/modules/$KREL/{osoyoo-dsi-panel,osoyoo-panel-regulator}.ko
   overlay       : $OVERLAYS_DIR/$PANEL.dtbo
   config.txt    : $OVERLAY_LINE
============================================================

A reboot is required. After it comes back, verify with:
   cat /sys/class/backlight/*/display_name      # should print: DSI-1
EOF

if [ -t 0 ]; then
  read -r -p "Reboot now? [y/N] " ans
  case "$ans" in
    [yY]|[yY][eE][sS]) note "Rebooting…"; sudo reboot ;;
    *) note "Not rebooting. Run 'sudo reboot' when ready." ;;
  esac
else
  note "Non-interactive shell — not rebooting. Run 'sudo reboot' when ready."
fi
