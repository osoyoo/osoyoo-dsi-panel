# OSOYOO DSI Panel Driver

Kernel driver for OSOYOO 720x1280 DSI touchscreen panels for Raspberry Pi.

**Supported Models:**
- 7-inch DSI touchscreen (720x1280)
Purchase Link: https://osoyoo.store/products/7-dsi-ips-720-1280-hd-capacitive-touchscreen-for-raspberry-pi-5-4b-3b-3a-dsi-gpio-powered-5-point-touch-6h-hardness-tempered-glass-anti-fingerprint?_pos=1&_psq=720&_ss=e&_v=1.0

- 10.1-inch DSI touchscreen (720x1280)
Purchase Link: https://osoyoo.store/products/osoyoo-10-1-ips-dsi-touchscreen-display-for-raspberry-pi-5-4-3-2

**Compatible Hardware:**
- Raspberry Pi 3 / Compute Module 3
- Raspberry Pi 4 / Compute Module 4
- Raspberry Pi 5 / Compute Module 5

---

## 🚀 Quick Start

### Installation (Recommended Method)

```bash
# Clone the repository
git clone https://github.com/osoyoo/osoyoo-dsi-panel.git
cd osoyoo-dsi-panel

# Install directly (no package building required)
sudo ./install-direct.sh
```

### Configuration

After installation, edit your config file:

```bash
sudo nano /boot/firmware/config.txt
# OR on older systems:
sudo nano /boot/config.txt
```

Add ONE of these lines:

```ini
# For 7-inch panel:
dtoverlay=osoyoo-panel-dsi-7inch

# For 10.1-inch panel:
dtoverlay=osoyoo-panel-dsi-10inch

# For 10.1-inch panel (4-lane mode):
dtoverlay=osoyoo-panel-dsi-10inch,4lane
```

Then reboot:
```bash
sudo reboot
```

---

## 📚 Documentation

- **[INSTALL-INSTRUCTIONS.md](INSTALL-INSTRUCTIONS.md)** - Complete installation guide (START HERE)
- **[TROUBLESHOOTING.md](TROUBLESHOOTING.md)** - Fix common issues (screen not displaying, etc.)
- **[HARDWARE-DETECTION-README.md](HARDWARE-DETECTION-README.md)** - How hardware auto-detection works
- **[APT-REPOSITORY-SETUP.md](APT-REPOSITORY-SETUP.md)** - Setting up APT repository
- **[DKMS-PACKAGE-README.md](DKMS-PACKAGE-README.md)** - DKMS package details

---

## ✨ Features

- ✅ **Automatic hardware detection** - Detects your Pi model (3/4/5) automatically
- ✅ **DKMS support** - Driver automatically rebuilds on kernel updates
- ✅ **Model-specific drivers** - Optimized drivers for each Pi generation
- ✅ **Easy installation** - Simple script-based installation
- ✅ **APT repository** - Optional installation via `sudo apt install`
- ✅ **Multiple panel sizes** - Supports both 7" and 10.1" displays

---

## 🛠️ Installation Methods

### Method 1: Direct Installation (Fastest)

```bash
git clone https://github.com/osoyoo/osoyoo-dsi-panel.git
cd osoyoo-dsi-panel
sudo ./install-direct.sh
```

**Pros:** Fastest, no package building, works everywhere
**Cons:** Manual updates (re-run script after git pull)

### Method 2: APT Repository

```bash
echo "deb [trusted=yes] https://osoyoo.github.io/osoyoo-dsi-panel/ ./" | \
    sudo tee /etc/apt/sources.list.d/osoyoo.list

sudo apt update
sudo apt install osoyoo-dsi-panel-dkms
```

**Pros:** Auto-updates via `apt upgrade`
**Cons:** Initial setup required

### Method 3: Build .deb Package

```bash
git clone https://github.com/osoyoo/osoyoo-dsi-panel.git
cd osoyoo-dsi-panel
./build-on-pi.sh
sudo dpkg -i ../osoyoo-dsi-panel-dkms_*.deb
```

**Pros:** Creates distributable package
**Cons:** Requires build tools

See **[INSTALL-INSTRUCTIONS.md](INSTALL-INSTRUCTIONS.md)** for detailed instructions.

---

## 🐛 Troubleshooting

### Screen not displaying anything?

1. **Check if modules are loaded:**
   ```bash
   lsmod | grep osoyoo
   ```

2. **Disable HDMI (force DSI):**
   Add to `/boot/firmware/config.txt`:
   ```ini
   hdmi_ignore_hotplug=1
   ```

3. **Check kernel logs:**
   ```bash
   dmesg | grep -i osoyoo
   dmesg | grep -i dsi
   ```

4. **Run diagnostics:**
   ```bash
   # Quick check
   lsmod | grep osoyoo
   dkms status osoyoo-dsi-panel
   grep osoyoo /boot/firmware/config.txt
   ```

**See [TROUBLESHOOTING.md](TROUBLESHOOTING.md) for complete guide.**

---

## 🔧 Manual Build (Old Method - Not Recommended)

<details>
<summary>Click to expand manual build instructions</summary>

### Driver build:
```bash
make
sudo cp ./osoyoo-panel-regulator.ko /lib/modules/$(uname -r)
sudo cp ./osoyoo-panel-dsi.ko /lib/modules/$(uname -r)
sudo depmod
sudo modprobe osoyoo-panel-regulator
sudo modprobe osoyoo-panel-dsi
```

### Device tree build:
```bash
# 7-inch
sudo dtc -I dts -O dtb -o osoyoo-panel-dsi-7inch.dtbo osoyoo-panel-dsi-7inch.dts
sudo cp osoyoo-panel-dsi-7inch.dtbo /boot/overlays/

# 10.1-inch
sudo dtc -I dts -O dtb -o osoyoo-panel-dsi-10inch.dtbo osoyoo-panel-dsi-10inch.dts
sudo cp osoyoo-panel-dsi-10inch.dtbo /boot/overlays/
```

**Note:** Manual building requires rebuilding after every kernel update. Use DKMS-based installation instead.

</details>

---

## 📦 Package Structure

```
osoyoo-dsi-panel/
├── src/
│   ├── pi3/          # Raspberry Pi 3 specific drivers
│   ├── pi4/          # Raspberry Pi 4 specific drivers
│   └── pi5/          # Raspberry Pi 5 specific drivers
├── debian/           # Debian package configuration
├── .github/          # GitHub Actions workflows
├── install-direct.sh # Direct installation script
├── build-on-pi.sh    # Package build script
├── Makefile          # Kernel module build
├── dkms.conf         # DKMS configuration
└── *.dts             # Device tree source files
```

---

## 🤝 Contributing

Issues and pull requests are welcome!

**Before submitting:**
1. Test on actual hardware (Pi 3/4/5)
2. Follow existing code style
3. Update documentation if needed

---

## 📄 License

GPL-2.0 - See LICENSE file for details

---

## 💬 Support

- **Issues:** https://github.com/osoyoo/osoyoo-dsi-panel/issues
- **Email:** support@osoyoo.info
- **Troubleshooting:** [TROUBLESHOOTING.md](TROUBLESHOOTING.md)

---

## 🔗 Links

- **APT Repository:** https://osoyoo.github.io/osoyoo-dsi-panel/
- **Installation Guide:** [INSTALL-INSTRUCTIONS.md](INSTALL-INSTRUCTIONS.md)
- **Releases:** https://github.com/osoyoo/osoyoo-dsi-panel/releases
