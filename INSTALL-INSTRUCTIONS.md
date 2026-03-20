# Installation Instructions - OSOYOO DSI Panel Driver

Due to macOS/Linux tar format incompatibility, we provide two installation methods:

## Method 1: Direct Installation (Recommended - Fastest)

This method installs directly from source without needing a .deb package.

### On your Raspberry Pi:

```bash
# Clone the repository
git clone https://github.com/osoyoo/osoyoo-dsi-panel.git
cd osoyoo-dsi-panel

# Run direct installation script
sudo ./install-direct.sh
```

The script will:
- Auto-detect your Pi model (Pi 3/4/5)
- Install dependencies
- Build and install the driver using DKMS
- Install device tree overlays

Then configure and reboot:
```bash
# Edit config
sudo nano /boot/firmware/config.txt  # or /boot/config.txt

# Add one of these lines:
dtoverlay=osoyoo-panel-dsi-7inch     # for 7" panel
# OR
dtoverlay=osoyoo-panel-dsi-10inch,dsi0,4lane    # for 10.1" panel in dsi0 of Pi 5 or CM5
# OR
dtoverlay=osoyoo-panel-dsi-10inch,dsi1,4lane    # for 10.1" panel in dsi1 of Pi 5 or CM5
# OR
dtoverlay=osoyoo-panel-dsi-10inch   # for 10.1" panel  Pi 3/4 or CM3/4

# Reboot
sudo reboot
```

---

## Method 2: Build .deb Package on Raspberry Pi

This creates a proper .deb package that can be distributed.

### On your Raspberry Pi:

```bash
# Clone the repository
git clone https://github.com/osoyoo/osoyoo-dsi-panel.git
cd osoyoo-dsi-panel

# Build the package
./build-on-pi.sh
```

This creates: `../osoyoo-dsi-panel-dkms_1.0-1_all.deb`

### Install the package:

```bash
sudo dpkg -i ../osoyoo-dsi-panel-dkms_1.0-1_all.deb
sudo apt-get install -f  # if needed
```

Then configure and reboot as in Method 1.

---

## Method 3: Download Source Tarball (No git required)

If you don't have git installed:

```bash
# Download source
wget https://github.com/osoyoo/osoyoo-dsi-panel/archive/refs/heads/master.zip
unzip master.zip
cd osoyoo-dsi-panel-master

# Install directly
sudo ./install-direct.sh
```

---

## Verification

After reboot, check if driver is loaded:

```bash
# Check modules
lsmod | grep osoyoo

# Should show:
# osoyoo_panel_dsi
# osoyoo_panel_regulator

# Check DKMS status
dkms status osoyoo-dsi-panel
```

---

## Troubleshooting

### Build fails with "kernel headers not found"

```bash
sudo apt-get install raspberrypi-kernel-headers
```

### Driver not loading after reboot

Check kernel logs:
```bash
dmesg | grep -i osoyoo
dmesg | grep -i dsi
```

### Wrong driver selected

The installer auto-detects your Pi model. To manually specify:
```bash
# Check detected model
cat /proc/device-tree/model

# Manually copy desired driver
sudo cp /usr/src/osoyoo-dsi-panel-1.0/src/pi4/*.c /usr/src/osoyoo-dsi-panel-1.0/
sudo cp /usr/src/osoyoo-dsi-panel-1.0/src/pi4/*.dts /usr/src/osoyoo-dsi-panel-1.0/
sudo dkms build -m osoyoo-dsi-panel -v 1.0
sudo dkms install -m osoyoo-dsi-panel -v 1.0
```

---

## Uninstallation

```bash
# Remove DKMS module
sudo dkms remove osoyoo-dsi-panel/1.0 --all

# Remove source files
sudo rm -rf /usr/src/osoyoo-dsi-panel-1.0

# Remove overlays
sudo rm -f /boot/overlays/osoyoo-panel-dsi-*.dtbo
sudo rm -f /boot/firmware/overlays/osoyoo-panel-dsi-*.dtbo

# Remove dtoverlay line from config.txt and reboot
```

---

## Why Not Use Pre-built .deb?

The .deb packages in the releases were built on macOS, which creates tar archives
in PAX format that are incompatible with Debian's dpkg. Building on Linux (Pi)
creates properly formatted packages.

For now, use **Method 1 (Direct Installation)** for quickest setup, or
**Method 2 (Build on Pi)** if you need a distributable .deb package.

---

## Support

- GitHub Issues: https://github.com/osoyoo/osoyoo-dsi-panel/issues
- Email: support@osoyoo.info
