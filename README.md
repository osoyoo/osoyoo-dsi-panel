# OSOYOO Raspberry Pi MIPI-DSI Panel Driver

One kernel driver for OSOYOO direct-MIPI-DSI touchscreens on Raspberry Pi.
Tested working on **Raspberry Pi 3 / 4 / 5** (and CM4 / CM5) with the panels below.

## Supported screens

| Panel | `<screen>` argument | Resolution | Lanes |
|---|---|---|---|
| 3.5" | `display3.5inch` | 480×800 | 2 |
| 7"   | `display7inch`   | 720×1280 | 2 |
| 10.1" | `display10inch` | 800×1280 | 2 on Pi 3/4, **4 on Pi 5** |

The driver and overlays are the same across boards — the board-specific details
(10.1" lane count, and the 3.5" pixel clock on Pi 5) are handled automatically.

---

## Quick install (recommended)

One command builds the modules against your running kernel, builds the overlay,
and sets `config.txt` — for any supported Pi and screen:

```bash
cd osoyoo-dsi-panel
./install-direct.sh <screen> [dsi0|dsi1]
```

Examples:

```bash
./install-direct.sh display10inch          # 10.1" on a Pi 3/4 (single DSI port)
./install-direct.sh display10inch dsi0      # 10.1" on a Pi 5, panel on the DISP0 port
./install-direct.sh display3.5inch dsi1     # 3.5"  on a Pi 5, panel on the DISP1 port
./install-direct.sh display7inch            # 7"    on a Pi 3/4
```

It asks to reboot at the end. After reboot, verify:

```bash
cat /sys/class/backlight/*/display_name     # prints a DSI connector, e.g. DSI-1 / DSI-2
sudo dmesg | grep -i osoyoo                  # shows the bound panel + lane count
```

### Which DSI port? (`dsi0` / `dsi1`)

- **Pi 3 / Pi 4** have a **single** DSI connector — omit the argument (defaults to `dsi1`).
- **Pi 5 / CM5** have **two** DSI connectors: **DISP0 = `dsi0`**, **DISP1 = `dsi1`**.
  Pass whichever one the ribbon is plugged into. If you're unsure, run without it;
  if the panel stays blank, re-run with the other port.

> **Cables:** Pi 5 uses 22-pin DISPLAY connectors (needs a 22→15-pin cable);
> Pi 4B / 3B+ / 3B use a 15-pin connector (15→15-pin cable).

---

## What the install handles for you

- **10.1" lane count** — 4 lanes on Pi 5 / CM5, 2 lanes on Pi 3/4, chosen from the board model.
- **3.5" on Pi 5** — Pi 5 drives DSI through RP1, whose D-PHY will **not lock** the low
  ~360 Mbps/lane link that the 3.5" panel's 30 MHz pixel clock produces on a Pi 4. The
  driver detects `bcm2712` (Pi 5 / CM5) and raises that panel's pixel clock to 40 MHz
  automatically, so the same driver works on both Pi 4 (30 MHz) and Pi 5 (40 MHz).
  Symptom if this is ever missing: **backlight on, but a blank screen**.
- **Kernel headers** — installed automatically if missing, and the build always targets
  the currently running kernel.

---

## Touch on a multi-display setup (Pi 5 with HDMI + DSI)

If you use the panel **on its own**, touch works out of the box.

If you also have an **HDMI monitor connected at the same time**, the desktop
compositor (labwc) may stretch the panel's touch across the *whole* multi-monitor
desktop, so taps land in the wrong place. Bind the touch device to the panel's
output in `~/.config/labwc/rc.xml`:

```bash
mkdir -p ~/.config/labwc
cp /etc/xdg/labwc/rc.xml ~/.config/labwc/rc.xml     # only if you don't already have one
```

Then add this line just before `</openbox_config>` (use the connector the panel is
on — `DSI-1` or `DSI-2`, as printed by `wlr-randr`):

```xml
<touch deviceName="Goodix Capacitive TouchScreen" mapToOutput="DSI-1" mouseEmulation="yes" />
```

Reload with `labwc --reconfigure` (or reboot). Note the device name must match what
`libinput list-devices` reports (e.g. `Goodix Capacitive TouchScreen`) — the stock
`rc.xml` ships i2c-prefixed names that may not match your device.

---

## Optional overlay parameters

Append after a comma, e.g. `./install-direct.sh display10inch dsi0` produces
`dtoverlay=osoyoo-panel-dsi-10inch,4lane,dsi0`. You can also add these by editing
the `dtoverlay=` line in `config.txt`:

- `rotation=90|180|270` — display rotation
- `invx` / `invy` / `swapxy` — touch axis fixes
- `disable_touch` — disable the touch input

---

## Manual install (reference)

The script does all of this; here it is by hand if you prefer. Run from the driver
folder, as your normal user (`sudo` only where shown).

> **Don't build as root** (`sudo su`). It leaves root-owned files that make a later
> non-`sudo` command fail with **"Permission denied"** — the most common install error.

```bash
# 1. Build the modules
make clean && make

# 2. Install them
sudo cp osoyoo-dsi-panel.ko osoyoo-panel-regulator.ko /lib/modules/$(uname -r)/
sudo depmod

# 3. Build the overlay straight into the boot overlays dir (pick YOUR panel's .dts)
PANEL=osoyoo-panel-dsi-10inch     # or osoyoo-panel-dsi-7inch / osoyoo-panel-st7701s-3p5inch
sudo dtc -@ -I dts -O dtb -o /boot/firmware/overlays/$PANEL.dtbo $PANEL.dts

# 4. Enable it in config.txt (add ,4lane for 10.1" on Pi 5; add ,dsi0 if on the DISP0 port)
echo "dtoverlay=$PANEL" | sudo tee -a /boot/firmware/config.txt
sudo reboot
```

`dtc` prints several `Warning (...)` lines — these are **normal**; only a `FATAL ERROR`
is a failure. On older Raspberry Pi OS the boot partition is `/boot/` instead of
`/boot/firmware/`.

---

## Troubleshooting

**Backlight on but blank screen, on a Pi 5, with the 3.5" panel.**
RP1's DSI won't lock the panel's low pixel clock. This is fixed automatically in the
current driver (it raises the 3.5" clock to 40 MHz on `bcm2712`). Make sure you built
and installed the **current** driver on the Pi 5 (`./install-direct.sh display3.5inch …`),
not an older copy.

**`FATAL ERROR: Couldn't open output file …dtbo: Permission denied`.**
A previous build left a root-owned `.dtbo`. The install writes straight into
`/boot/firmware/overlays/` with `sudo` to avoid this. If a stale local copy remains:
`sudo rm -f *.dtbo`.

**`display_name` doesn't print a `DSI-*` connector after reboot.**
- Check the `dtoverlay=` line in `config.txt` matches your panel — and on Pi 5, that
  `,4lane` (10.1") and/or the right `,dsi0`/`,dsi1` port are set.
- Confirm modules are installed: `ls /lib/modules/$(uname -r)/osoyoo-*.ko`, then `sudo depmod`.
- `sudo dmesg | grep -i osoyoo` shows the compatible string and lane count it bound with.

**Touch position is off on a multi-display (HDMI + DSI) setup.**
See *Touch on a multi-display setup* above.

**`make` fails.** Install the kernel headers and rebuild:

```bash
sudo apt update && sudo apt install -y raspberrypi-kernel-headers
make clean && make
```

---

## Files

- `osoyoo-dsi-panel.c` → `osoyoo-dsi-panel.ko` — the panel driver (all sizes)
- `osoyoo-panel-regulator.c` → `osoyoo-panel-regulator.ko` — STM32 companion (reset + backlight)
- `osoyoo-panel-*.dts` — device-tree overlays (3.5" / 7" / 10.1")
- `install-direct.sh` — one-command build + install
- `Makefile`
