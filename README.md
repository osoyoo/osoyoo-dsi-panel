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

1. Installs build dependencies (`dkms`, `device-tree-compiler`, kernel headers).
2. Detects your Raspberry Pi model and picks the matching driver source.
3. Builds and installs the kernel modules via DKMS (so they rebuild automatically on
   kernel updates).
4. Installs the device-tree overlays.
5. **Writes the correct `dtoverlay=` line into `config.txt` for your board** — 2-lane
   on Pi 3 / Pi 4, 4-lane on Pi 5 — backing the file up first.
6. Offers to reboot.

After it finishes, **reboot** and the panel comes up.

### 7-inch panel

The default is the 10.1" panel. For the 7" panel:

```bash
sudo ./install-direct.sh 7inch
```

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
```

Then remove the `dtoverlay=osoyoo-panel-dsi-*` line from `/boot/firmware/config.txt`
(a `config.txt.osoyoo.bak` backup is written on each install) and reboot.

## Repository layout

```
install-direct.sh          # the installer (run this)
Makefile, dkms.conf        # DKMS build config
src/pi3/  src/pi4/  src/pi5/
    osoyoo-panel-dsi.c            # panel driver
    osoyoo-panel-regulator.c     # backlight/regulator driver
    osoyoo-panel-dsi-7inch.dts   # 7" overlay source
    osoyoo-panel-dsi-10inch.dts  # 10.1" overlay source (2-lane default, ,4lane param)
```
