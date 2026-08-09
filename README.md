# OSOYOO Raspberry Pi MIPI-DSI Panel Driver

One kernel driver for OSOYOO direct-MIPI-DSI touchscreens on Raspberry Pi. Tested working on
every panel listed below.

## Supported screens

| Your panel | `.dts` source file | `dtoverlay=` line for `config.txt` | Resolution | Lanes |
|---|---|---|---|---|
| 3.5" | `osoyoo-panel-st7701s-3p5inch.dts` | `dtoverlay=osoyoo-panel-st7701s-3p5inch` | 480×800 | 2 |
| 7" | `osoyoo-panel-dsi-7inch.dts` | `dtoverlay=osoyoo-panel-dsi-7inch` | 720×1280 | 2 |
| 10.1" (Pi 3 / Pi 4) | `osoyoo-panel-dsi-10inch.dts` | `dtoverlay=osoyoo-panel-dsi-10inch` | 800×1280 | 2 |
| 10.1" (Pi 5 / CM5, 4-lane) | `osoyoo-panel-dsi-10inch.dts` | `dtoverlay=osoyoo-panel-dsi-10inch,4lane` | 800×1280 | 4 |

Pick the **one** row that matches your screen and board. You'll use its `.dts` file in Step 3 and
its `dtoverlay=` line in Step 4. The rest of the steps are identical for every panel.

Optional overlay parameters (append after a comma, e.g. `...,rotation=90`): `dsi0` (use DSI0 instead
of DSI1), `rotation=90|180|270` (display rotation), `invx` / `invy` / `swapxy` (touch axis fix),
`disable_touch`.

**DSI port note:** the overlays default to **DSI1**. Pi 5 / CM5 / CM4 expose two DSI connectors
(DISP0/DISP1); if the panel is on the **DISP0 / DSI0** port, append `,dsi0`
(e.g. `dtoverlay=osoyoo-panel-dsi-10inch,dsi0`). On Compute Module carriers there is **no DSI
autodetect** — the `dtoverlay=` line must be present in `config.txt`. Pi 5 uses 22-pin DISPLAY
connectors (22→15-pin cable); Pi 4B / 3B+ / 3B / 2B use a 15-pin connector (15→15-pin cable).

## Files

- `osoyoo-dsi-panel.c` → `osoyoo-dsi-panel.ko` — the panel driver (all sizes)
- `osoyoo-panel-regulator.c` → `osoyoo-panel-regulator.ko` — companion device (reset)
- `osoyoo-panel-*.dts` — device-tree overlays
- `Makefile`

---

## Installation

Run every command from inside the driver folder:

```
cd ~/osoyoo-dsi-driver
```

> **Do not use `sudo su` / a root shell.** Build as your normal user; use `sudo` only on the exact
> commands shown with it below. Building as root leaves root-owned files in the folder that make a
> later non-`sudo` command fail with **"Permission denied"** — the single most common install error.

### Step 1 — Build the two kernel modules (normal user, no `sudo`)

```
make clean && make
```

This produces `osoyoo-dsi-panel.ko` and `osoyoo-panel-regulator.ko`. Warnings are fine; a failure
here is an error.

### Step 2 — Install the modules

```
sudo cp osoyoo-dsi-panel.ko osoyoo-panel-regulator.ko /lib/modules/$(uname -r)/
sudo depmod
```

### Step 3 — Build the overlay **straight into** the boot folder

Set `PANEL` to the `.dts` name from the table (drop the `.dts`), then run the two commands as-is.
The example below is for the **10.1" panel**; for the 7" use `osoyoo-panel-dsi-7inch`, for the 3.5"
use `osoyoo-panel-st7701s-3p5inch`.

```
PANEL=osoyoo-panel-dsi-10inch
sudo dtc -@ -I dts -O dtb -o /boot/firmware/overlays/$PANEL.dtbo $PANEL.dts
```

`dtc` prints several `Warning (...)` lines (about `reg_format`, unit addresses, etc.) — **these are
normal and harmless.** The build succeeded as long as the command ends without a `FATAL ERROR`.

Writing the `.dtbo` directly into `/boot/firmware/overlays/` with `sudo` (instead of building a local
copy first and copying it) avoids the classic root-owned-`.dtbo` "Permission denied" trap entirely.

> On older Raspberry Pi OS the boot partition is mounted at `/boot/` instead of `/boot/firmware/`.
> If `/boot/firmware/` does not exist on your system, use `/boot/overlays/` in the command above.

### Step 4 — Enable the overlay and reboot

Append your panel's `dtoverlay=` line (from the table) to `config.txt`. Example for the 10.1":

```
echo "dtoverlay=osoyoo-panel-dsi-10inch" | sudo tee -a /boot/firmware/config.txt
sudo reboot
```

(Use `/boot/config.txt` if your system doesn't have `/boot/firmware/`.)

---

## Verify

After the reboot:

```
cat /sys/class/backlight/*/display_name      # should print: DSI-1
```

If it prints `DSI-1`, the driver is bound. You can confirm further with:

```
lsmod | grep osoyoo                          # both modules loaded
sudo dmesg | grep -i osoyoo                   # shows the detected panel + lane count
```

The desktop brightness slider is then at **Screen Configuration → Screen → DSI-1 → Brightness**.
Touch follows rotation after you tick the touch device under **Screen Configuration → Screen →
DSI-1 → Touchscreen** and then set the Orientation.

---

## Troubleshooting

**`FATAL ERROR: Couldn't open output file ...dtbo: Permission denied` (Step 3).**
A previous build left a root-owned `.dtbo` in the folder, so a non-`sudo` `dtc` can't overwrite it.
The Step 3 command above avoids this by writing straight to `/boot/firmware/overlays/` with `sudo`.
If you still have a stale local copy, remove it and rebuild:

```
sudo rm -f *.dtbo
```

**`display_name` doesn't print `DSI-1` after reboot.**
- Re-check that the `dtoverlay=` line in `config.txt` exactly matches your panel (and, on Pi 5 / CM,
  that you added `,4lane` and/or `,dsi0` as needed — CM carriers have no DSI autodetect).
- Confirm the modules are in place: `ls /lib/modules/$(uname -r)/osoyoo-*.ko`, then `sudo depmod`.
- Look for the panel line in `sudo dmesg | grep -i osoyoo` — it reports the compatible string and
  lane count it bound with.

**`make` fails.** Install the kernel headers, then rebuild:

```
sudo apt update && sudo apt install -y raspberrypi-kernel-headers
make clean && make
```

---

## Removing older per-panel drivers

If you previously installed the standalone drivers, remove them so they don't clash with this
unified driver over the same compatible strings:

```
sudo rm -f /lib/modules/$(uname -r)/osoyoo-panel-dsi.ko /lib/modules/$(uname -r)/osoyoo-panel-st7701s.ko
sudo depmod
```
