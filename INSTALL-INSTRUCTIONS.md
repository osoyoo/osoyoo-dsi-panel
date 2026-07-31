# OSOYOO DSI Panel Driver

Driver + device-tree overlays for the **OSOYOO DSI touch displays** (7" and 10.1")
on Raspberry Pi. One command builds and installs the driver with DKMS, installs the
overlay, and configures `config.txt` automatically for your board.

## Supported hardware

| Board                       | 10.1" panel | Overlay set automatically                     |
|-----------------------------|-------------|-----------------------------------------------|
| Raspberry Pi 3 / CM3        | 2-lane DSI  | `dtoverlay=osoyoo-panel-dsi-10inch`           |
| Raspberry Pi 4 / CM4        | 2-lane DSI  | `dtoverlay=osoyoo-panel-dsi-10inch`           |
| Raspberry Pi 5 / CM5        | 4-lane DSI  | `dtoverlay=osoyoo-panel-dsi-10inch,4lane`     |

The 7" panel is 2-lane on every board.

Works on Raspberry Pi OS (Bookworm/Trixie) and Debian/Ubuntu-based images, on both
older and current kernels (the driver builds on kernel 6.15+ as well as earlier releases).

## Install

```bash
git clone https://github.com/osoyoo/osoyoo-dsi-panel.git
cd osoyoo-dsi-panel
sudo ./install-direct.sh
```

That's it. The script:

1. Installs build dependencies (`dkms`, `device-tree-compiler`, `i2c-tools`, kernel headers).
2. Detects your Raspberry Pi model and picks the matching driver source.
3. Builds and installs the kernel modules via DKMS (so they rebuild automatically on
   kernel updates).
4. Installs the device-tree overlays.
5. **Writes the correct `dtoverlay=` line into `config.txt`** for your board (2-lane on
   Pi 3 / Pi 4, 4-lane on Pi 5), backing the file up first.
6. Installs a boot-time **auto-detect service** that figures out whether a 7" or 10.1"
   panel is connected (see below).
7. Offers to reboot.

After it finishes, **reboot** and the panel comes up.

### Automatic 7" vs 10.1" detection

You don't need to tell the installer which panel you have. The 7" and 10.1" panels use
different Goodix touch controllers (**GT911** on the 7", **GT9271** on the 10.1"), and the
installed `osoyoo-panel-detect` service reads that chip ID over I2C at boot to identify the
physical panel, then makes `config.txt` match it:

- On first boot the panel is powered by a default overlay so the touch controller can be read.
- If the connected panel is the *other* size, the service corrects `config.txt` and reboots
  **once** automatically. After that it is a no-op on every boot (and will re-correct again
  if you ever swap panels).

This means the **same `install-direct.sh` command works for either panel** on Pi 3 / Pi 4 / Pi 5.

### Forcing a panel (optional)

To skip auto-detection and pin a specific panel:

```bash
sudo ./install-direct.sh 7inch     # force the 7" panel
sudo ./install-direct.sh 10inch    # force the 10.1" panel
```

Forcing writes `/etc/osoyoo-panel-detect.disabled`, which turns the auto-detect service off so
your choice sticks. Delete that file (or re-run with no argument) to re-enable auto-detection.

The script is safe to re-run — it replaces its own overlay line instead of duplicating it.

## Verify

After rebooting:

```bash
# Modules loaded
lsmod | grep osoyoo

# DSI connector present and enabled
cat /sys/class/drm/card*-DSI-1/status     # -> connected
cat /sys/class/drm/card*-DSI-1/enabled    # -> enabled

# Kernel log
sudo dmesg | grep -i osoyoo
```

## Troubleshooting

**Backlight is on but the screen is black.**
Almost always a DSI **lane-count mismatch**. The installer sets lanes by board, but if
you have a non-standard cable, force the other mode by editing the `dtoverlay=` line in
`/boot/firmware/config.txt` and rebooting:

- 2-lane: `dtoverlay=osoyoo-panel-dsi-10inch`
- 4-lane: `dtoverlay=osoyoo-panel-dsi-10inch,4lane`

Also check the DSI ribbon cable is fully seated and the right way round at both the Pi
and the panel board.

**Build failed / "Could not find kernel headers".**
Install headers, then re-run the installer:

```bash
sudo apt-get install raspberrypi-kernel-headers   # Raspberry Pi OS
# or:  sudo apt-get install linux-headers-$(uname -r)
```

**"DKMS tree already contains osoyoo-dsi-panel/1.0".**
A previous run is still registered. Clear it and re-run:

```bash
sudo dkms remove osoyoo-dsi-panel/1.0 --all
sudo ./install-direct.sh
```

## Uninstall

```bash
sudo dkms remove osoyoo-dsi-panel/1.0 --all
sudo systemctl disable --now osoyoo-panel-detect.service
sudo rm -f /usr/local/sbin/osoyoo-panel-detect \
           /etc/systemd/system/osoyoo-panel-detect.service \
           /etc/osoyoo-panel-detect.disabled
```

Then remove the `dtoverlay=osoyoo-panel-dsi-*` line from `/boot/firmware/config.txt`
(a `config.txt.osoyoo.bak` backup is written on each install) and reboot.

## Repository layout

```
install-direct.sh            # the installer (run this)
osoyoo-panel-detect.sh       # boot-time 7"/10.1" auto-detect (installed as a service)
osoyoo-panel-detect.service  # systemd unit that runs the detector at boot
Makefile, dkms.conf          # DKMS build config
src/pi3/  src/pi4/  src/pi5/
    osoyoo-panel-dsi.c            # panel driver
    osoyoo-panel-regulator.c     # backlight/regulator driver
    osoyoo-panel-dsi-7inch.dts   # 7" overlay source  (Goodix GT911 touch)
    osoyoo-panel-dsi-10inch.dts  # 10.1" overlay source (Goodix GT9271; 2-lane default, ,4lane param)
```
