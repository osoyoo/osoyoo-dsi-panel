# Troubleshooting Guide - OSOYOO DSI Panel

## Issue: Display Not Working After Installation

If the driver installed successfully but the DSI screen shows nothing (while HDMI works), follow these steps:

### Step 1: Verify Driver Installation

Check if the kernel modules are loaded:

```bash
lsmod | grep osoyoo
```

**Expected output:**
```
osoyoo_panel_dsi       16384  0
osoyoo_panel_regulator 16384  1 osoyoo_panel_dsi
```

If modules are NOT loaded, try loading them manually:
```bash
sudo modprobe osoyoo-panel-regulator
sudo modprobe osoyoo-panel-dsi
```

### Step 2: Check DKMS Status

```bash
dkms status osoyoo-dsi-panel
```

**Expected output:**
```
osoyoo-dsi-panel/1.0, 6.12.75+rpt-rpi-v8, arm64: installed
```

If not installed, rebuild:
```bash
sudo dkms install osoyoo-dsi-panel/1.0
```

### Step 3: Verify Device Tree Overlay

Check if overlay file exists:
```bash
ls -l /boot/firmware/overlays/osoyoo-panel-dsi-10inch.dtbo
# OR
ls -l /boot/overlays/osoyoo-panel-dsi-10inch.dtbo
```

Check config.txt has the overlay:
```bash
grep osoyoo /boot/firmware/config.txt
```

Should show:
```
dtoverlay=osoyoo-panel-dsi-10inch
```

### Step 4: Check Kernel Logs

Look for DSI-related errors:
```bash
dmesg | grep -i dsi
dmesg | grep -i osoyoo
dmesg | grep -i panel
```

Common issues to look for:
- `DSI: No panel detected`
- `failed to register panel`
- `regulator not found`
- `probe failed`

### Step 5: Check DSI Hardware Connection

```bash
# Check DSI host
ls -l /sys/class/drm/

# Check for DSI device
cat /proc/device-tree/soc/dsi*/status
```

Should show `okay` for at least one DSI controller.

### Step 6: Disable HDMI (Force DSI Primary)

The Raspberry Pi might be prioritizing HDMI over DSI. Try disabling HDMI:

**Edit `/boot/firmware/config.txt`:**

```bash
sudo nano /boot/firmware/config.txt
```

**Add these lines:**

```ini
# Disable HDMI
hdmi_ignore_hotplug=1

# Force DSI as primary display
dtoverlay=osoyoo-panel-dsi-10inch

# Optional: Disable FKMS (use legacy graphics)
#dtoverlay=vc4-fkms-v3d
# Enable this instead if using newer kernel:
dtoverlay=vc4-kms-v3d
```

**OR try disabling KMS entirely:**

```ini
# Comment out or remove:
#dtoverlay=vc4-kms-v3d
#dtoverlay=vc4-fkms-v3d

# Add:
dtoverlay=osoyoo-panel-dsi-10inch
hdmi_ignore_hotplug=1
```

Then reboot:
```bash
sudo reboot
```

### Step 7: Check for Conflicting Overlays

Some overlays conflict with DSI. Check your config.txt for:

```bash
cat /boot/firmware/config.txt | grep dtoverlay
```

**Remove or comment out these if present:**
- `dtoverlay=vc4-fkms-v3d` (can conflict with DSI)
- Any other display-related overlays

### Step 8: Verify Power Supply

DSI panels need sufficient power. Make sure you're using:
- **Raspberry Pi 4**: 5V/3A minimum (official Pi power supply recommended)
- Check for undervoltage warnings:
  ```bash
  vcgencmd get_throttled
  ```
  Output `0x0` means no throttling. Anything else indicates power issues.

### Step 9: Test with Different Display Mode

Try 4-lane mode if using 2-lane, or vice versa:

```bash
sudo nano /boot/firmware/config.txt
```

Change:
```ini
# From:
dtoverlay=osoyoo-panel-dsi-10inch

# To (4-lane mode):
dtoverlay=osoyoo-panel-dsi-10inch,4lane
```

Reboot and test.

### Step 10: Check Display Rotation/Orientation

Sometimes the display is on but rotated incorrectly. Try:

```bash
sudo nano /boot/firmware/config.txt
```

Add:
```ini
display_rotate=0
# Or try: display_rotate=1, 2, or 3
```

### Step 11: Verify DSI Cable Connection

**Hardware checklist:**
- [ ] DSI cable properly seated on both ends
- [ ] Blue tab on cable facing correct direction
- [ ] Cable not damaged or twisted
- [ ] Using the correct DSI port (Pi 4 has DSI0 and DSI1)
- [ ] Try the other DSI port if Pi has two

### Step 12: Check for Known Kernel Issues

Kernel 6.12+ might have DSI-related changes. Check compatibility:

```bash
uname -r
# Your kernel: 6.12.75+rpt-rpi-v8
```

Try downgrading to known-good kernel:
```bash
sudo rpi-update stable
sudo reboot
```

### Step 13: Test with Minimal config.txt

Create a minimal config.txt for testing:

```bash
sudo cp /boot/firmware/config.txt /boot/firmware/config.txt.backup
sudo nano /boot/firmware/config.txt
```

**Minimal config:**
```ini
[all]
kernel=kernel8.img
arm_64bit=1

# Disable HDMI
hdmi_ignore_hotplug=1

# DSI Panel
dtoverlay=osoyoo-panel-dsi-10inch

# NO vc4-kms overlays for testing
```

Reboot and test. If this works, gradually add back other settings.

### Step 14: Check Panel Power Regulator

```bash
# Check if regulator is registered
ls -l /sys/class/regulator/

# Check regulator status
cat /sys/kernel/debug/regulator/regulator_summary
```

Look for the panel regulator in the list.

### Step 15: Force DSI Detection

Some panels need manual initialization. Check if this helps:

```bash
sudo nano /boot/firmware/config.txt
```

Add:
```ini
# Force DSI detection
ignore_lcd=0
disable_fw_kms_setup=1
```

### Step 16: Collect Diagnostic Information

If still not working, collect this info for further debugging:

```bash
# Create diagnostic file
cat > ~/dsi-diagnostics.txt << 'EOF'
=== System Info ===
EOF

uname -a >> ~/dsi-diagnostics.txt
cat /proc/device-tree/model >> ~/dsi-diagnostics.txt

echo -e "\n=== DKMS Status ===" >> ~/dsi-diagnostics.txt
dkms status >> ~/dsi-diagnostics.txt

echo -e "\n=== Loaded Modules ===" >> ~/dsi-diagnostics.txt
lsmod | grep osoyoo >> ~/dsi-diagnostics.txt

echo -e "\n=== Config.txt ===" >> ~/dsi-diagnostics.txt
grep -v "^#" /boot/firmware/config.txt | grep -v "^$" >> ~/dsi-diagnostics.txt

echo -e "\n=== Device Tree ===" >> ~/dsi-diagnostics.txt
ls -l /boot/firmware/overlays/osoyoo* >> ~/dsi-diagnostics.txt

echo -e "\n=== DSI Kernel Logs ===" >> ~/dsi-diagnostics.txt
dmesg | grep -i dsi >> ~/dsi-diagnostics.txt

echo -e "\n=== Panel Kernel Logs ===" >> ~/dsi-diagnostics.txt
dmesg | grep -i panel >> ~/dsi-diagnostics.txt

echo -e "\n=== OSOYOO Kernel Logs ===" >> ~/dsi-diagnostics.txt
dmesg | grep -i osoyoo >> ~/dsi-diagnostics.txt

echo -e "\n=== DRM Devices ===" >> ~/dsi-diagnostics.txt
ls -l /sys/class/drm/ >> ~/dsi-diagnostics.txt

echo -e "\n=== Regulators ===" >> ~/dsi-diagnostics.txt
ls -l /sys/class/regulator/ >> ~/dsi-diagnostics.txt

cat ~/dsi-diagnostics.txt
```

Share this output for further assistance.

---

## Common Solutions Summary

### Solution 1: Disable HDMI (Most Common)
```bash
# /boot/firmware/config.txt
hdmi_ignore_hotplug=1
dtoverlay=osoyoo-panel-dsi-10inch
```

### Solution 2: Disable KMS
```bash
# /boot/firmware/config.txt
# Comment out:
#dtoverlay=vc4-kms-v3d

# Keep:
dtoverlay=osoyoo-panel-dsi-10inch
hdmi_ignore_hotplug=1
```

### Solution 3: Try 4-Lane Mode
```bash
# /boot/firmware/config.txt
dtoverlay=osoyoo-panel-dsi-10inch,4lane
```

### Solution 4: Power Issues
- Use official Raspberry Pi power supply (5V/3A)
- Check for lightning bolt icon (undervoltage)

### Solution 5: DSI Cable
- Reseat cable on both ends
- Try other DSI port (if Pi has two)
- Replace cable if damaged

---

## Quick Diagnostic Script

Run this to quickly check common issues:

```bash
#!/bin/bash
echo "=== Quick DSI Diagnostics ==="
echo ""

echo "1. Modules loaded:"
lsmod | grep osoyoo || echo "  ❌ Modules NOT loaded"
echo ""

echo "2. DKMS status:"
dkms status osoyoo-dsi-panel || echo "  ❌ Not in DKMS"
echo ""

echo "3. Overlay file:"
ls -l /boot/firmware/overlays/osoyoo-panel-dsi-10inch.dtbo 2>/dev/null || \
ls -l /boot/overlays/osoyoo-panel-dsi-10inch.dtbo 2>/dev/null || \
echo "  ❌ Overlay file NOT found"
echo ""

echo "4. Config.txt:"
grep osoyoo /boot/firmware/config.txt || echo "  ❌ NOT in config.txt"
echo ""

echo "5. HDMI status:"
grep hdmi_ignore /boot/firmware/config.txt && echo "  ✓ HDMI disabled" || \
echo "  ⚠️  HDMI not disabled (may conflict)"
echo ""

echo "6. Recent DSI errors:"
dmesg | grep -i dsi | tail -5
echo ""

echo "7. Power status:"
vcgencmd get_throttled
echo "  (0x0 = OK, anything else = power issue)"
```

Save as `check-dsi.sh`, make executable with `chmod +x check-dsi.sh`, and run with `./check-dsi.sh`.

---

## Getting Help

If none of these steps work, please provide:

1. Output from Step 16 (diagnostic information)
2. Your complete `/boot/firmware/config.txt`
3. Output of `dmesg | grep -i dsi`
4. Photo of your DSI connection
5. Panel model (7" or 10.1")

Contact:
- GitHub Issues: https://github.com/osoyoo/osoyoo-dsi-panel/issues
- Email: support@osoyoo.info
