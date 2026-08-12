#!/usr/bin/env bash
#
# install-direct.sh — build + install the OSOYOO unified MIPI-DSI panel driver
#                     on Raspberry Pi 3 / 4 / 5 (and CM4 / CM5).
#
# Usage:
#   ./install-direct.sh <screen> [dsi0|dsi1]
#
#   <screen> is one of:
#     display3.5inch    3.5"   480x800    (ST7701S,  2-lane)
#     display7inch      7"     720x1280   (ILI9881C, 2-lane)
#     display10inch     10.1"  800x1280   (ILI9881C, 2-lane on Pi 3/4, 4-lane on Pi 5)
#
#   [dsi0|dsi1] — which DSI connector the panel is plugged into (optional):
#     * Pi 3 / Pi 4 have ONE DSI connector — omit this (defaults to dsi1).
#     * Pi 5 / CM5 have TWO (DISP0 = dsi0, DISP1 = dsi1). Pass the one you used.
#       If unsure, run without it; if the panel stays blank, re-run with the other.
#
# What it does automatically:
#   1) Detects the running kernel and builds the modules against ITS headers
#      (installs the headers package first if it is missing).
#   2) Detects the board and picks the 10.1" lane count (4-lane on Pi 5 / CM5,
#      2-lane otherwise).
#   3) Picks the right .dts/.dtbo for the screen, builds the overlay into the
#      boot overlays dir, and sets the matching dtoverlay= line in config.txt.
#
# The 3.5" panel's Pi-5 pixel-clock quirk (RP1's DSI D-PHY won't lock the low
# 30 MHz link) is handled inside the driver itself — it raises the clock on
# bcm2712/RP1 automatically. Nothing to configure here.
#
# Run as your NORMAL user (it uses sudo only where root is required).

set -euo pipefail

# --------------------------------------------------------------------------
# 0. Locate ourselves + the source tree
# --------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

die()  { echo "ERROR: $*" >&2; exit 1; }
note() { echo ">> $*"; }

usage() {
  cat >&2 <<EOF
Usage: $(basename "$0") <screen> [dsi0|dsi1]

  screen:
    display3.5inch    3.5"   panel
    display7inch      7"     panel
    display10inch     10.1"  panel

  dsi0|dsi1 (optional): which DSI connector the panel is on.
    Pi 3 / Pi 4  -> omit (single connector, defaults to dsi1).
    Pi 5 / CM5   -> DISP0 = dsi0, DISP1 = dsi1.

Examples:
  ./$(basename "$0") display10inch          # 10.1" on a Pi 4
  ./$(basename "$0") display10inch dsi0      # 10.1" on a Pi 5, DISP0 port
  ./$(basename "$0") display3.5inch dsi1     # 3.5" on a Pi 5, DISP1 port
EOF
  exit 1
}

[ $# -ge 1 ] && [ $# -le 2 ] || usage

# --------------------------------------------------------------------------
# 1. Map <screen> -> overlay base name (the .dts/.dtbo share this stem)
# --------------------------------------------------------------------------
case "$1" in
  display10inch|display10|10inch|10.1inch)
      PANEL="osoyoo-panel-dsi-10inch";        LABEL="10.1\"" ;;
  display7inch|display7|7inch)
      PANEL="osoyoo-panel-dsi-7inch";         LABEL="7\"" ;;
  display3.5inch|display3p5inch|3.5inch|3p5inch)
      PANEL="osoyoo-panel-st7701s-3p5inch";   LABEL="3.5\"" ;;
  *)
      echo "Unknown screen: '$1'" >&2
      usage ;;
esac

# Optional DSI port argument (default: dsi1 = the overlay default).
PORT="dsi1"
if [ $# -eq 2 ]; then
  case "$2" in
    dsi0|DSI0) PORT="dsi0" ;;
    dsi1|DSI1) PORT="dsi1" ;;
    *) die "second argument must be 'dsi0' or 'dsi1' (got '$2')" ;;
  esac
fi

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
# 3. Build the dtoverlay= line: base name + optional params.
#    - 10.1" on Pi 5 / CM5 uses 4 lanes (2 lanes elsewhere = overlay default).
#    - dsi0 is appended only when the panel is on the DISP0 / DSI0 connector.
# --------------------------------------------------------------------------
PARAMS=""
MODEL="$(tr -d '\0' < /proc/device-tree/model 2>/dev/null || true)"
note "Board: ${MODEL:-unknown}"

if [ "$PANEL" = "osoyoo-panel-dsi-10inch" ]; then
  case "$MODEL" in
    *"Raspberry Pi 5"*|*"Compute Module 5"*)
        PARAMS="${PARAMS},4lane"; note "10.1\" on Pi 5 -> 4-lane" ;;
    *)  note "10.1\" -> 2-lane (default)" ;;
  esac
fi

if [ "$PORT" = "dsi0" ]; then
  PARAMS="${PARAMS},dsi0"; note "DSI port -> DSI0 (DISP0)"
else
  note "DSI port -> DSI1 (DISP1, default)"
fi

OVERLAY_LINE="dtoverlay=${PANEL}${PARAMS}"

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
# 5. Build the modules against the detected kernel
#    KDIR=... overrides the Makefile's value so we always target THIS kernel.
# --------------------------------------------------------------------------
note "Building modules (make KDIR=$KBUILD)…"
make clean
make KDIR="$KBUILD"

for ko in osoyoo-dsi-panel.ko osoyoo-panel-regulator.ko; do
  [ -s "$SCRIPT_DIR/$ko" ] || die "build did not produce $ko"
done

# --------------------------------------------------------------------------
# 6. Install the modules
# --------------------------------------------------------------------------
note "Installing modules into /lib/modules/$KREL/ …"
sudo cp "$SCRIPT_DIR/osoyoo-dsi-panel.ko" "$SCRIPT_DIR/osoyoo-panel-regulator.ko" "/lib/modules/$KREL/"
sudo depmod "$KREL"

# --------------------------------------------------------------------------
# 7. Build the overlay straight into the boot overlays dir
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
# 8. Set the dtoverlay= line in config.txt, idempotently
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
   board         : ${MODEL:-unknown}
   kernel        : $KREL
   modules       : /lib/modules/$KREL/{osoyoo-dsi-panel,osoyoo-panel-regulator}.ko
   overlay       : $OVERLAYS_DIR/$PANEL.dtbo
   config.txt    : $OVERLAY_LINE
============================================================

A reboot is required. After it comes back, verify with:
   cat /sys/class/backlight/*/display_name    # prints a DSI connector, e.g. DSI-1 / DSI-2
   sudo dmesg | grep -i osoyoo                 # shows the bound panel + lane count
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
