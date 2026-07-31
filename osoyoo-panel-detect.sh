#!/bin/bash
# osoyoo-panel-detect — identify the connected OSOYOO DSI panel and make config.txt match.
#
# The 7" and 10.1" panels use different Goodix touch controllers at I2C 0x5d:
#   GT911  (product id "911")  -> 7"    -> dtoverlay=osoyoo-panel-dsi-7inch
#   GT9271 (product id "9271") -> 10.1" -> dtoverlay=osoyoo-panel-dsi-10inch[,4lane on Pi 5]
# The product id is stored in the controller's silicon, so reading it identifies the
# physical panel regardless of which overlay is currently loaded.
#
# The DSI touch controller is only powered once a panel overlay is active, so this
# runs at boot (osoyoo-panel-detect.service), not at install time. If the loaded
# overlay does not match the connected panel it rewrites config.txt and reboots once.
#
# Safe and idempotent: it only reboots when it actually changes config.txt, so it
# converges after at most one corrective reboot, and it is a no-op when already correct.
# It also leaves config.txt untouched if it cannot positively identify the panel.

set -u

TOUCH_ADDR=0x5d
DISABLE_MARKER=/etc/osoyoo-panel-detect.disabled

DRYRUN=0
[ "${1:-}" = "--dry-run" ] && DRYRUN=1

log() { echo "osoyoo-panel-detect: $*"; }

# Respect an explicit panel choice made at install time (sudo ./install-direct.sh 7inch|10inch)
if [ -f "$DISABLE_MARKER" ]; then
    log "auto-detect disabled by $DISABLE_MARKER (panel was set explicitly); nothing to do"
    exit 0
fi

# Locate config.txt
CFG=/boot/firmware/config.txt
[ -f "$CFG" ] || CFG=/boot/config.txt
[ -f "$CFG" ] || { log "no config.txt found; nothing to do"; exit 0; }

# Detect Pi model (only needed to choose 2-lane vs 4-lane for the 10" panel)
pi_model="unknown"
if [ -f /proc/device-tree/model ]; then
    m=$(tr -d '\0' < /proc/device-tree/model)
    case "$m" in
        *"Raspberry Pi 5"*|*"Compute Module 5"*) pi_model=pi5 ;;
        *"Raspberry Pi 4"*|*"Compute Module 4"*) pi_model=pi4 ;;
        *"Raspberry Pi 3"*|*"Compute Module 3"*) pi_model=pi3 ;;
    esac
fi

# Read the Goodix product id (reg 0x8140, 4 ASCII bytes) from whichever i2c bus answers.
read_product_id() {
    local bus raw byte d ascii
    for bus in $(ls /dev/i2c-* 2>/dev/null | sed 's|/dev/i2c-||' | sort -n); do
        raw=$(i2ctransfer -f -y "$bus" w2@${TOUCH_ADDR} 0x81 0x40 r4 2>/dev/null) || continue
        [ -n "$raw" ] || continue
        ascii=""
        for byte in $raw; do
            d=$((byte))
            if [ "$d" -ge 48 ] && [ "$d" -le 122 ]; then
                ascii="${ascii}$(printf "\\$(printf '%03o' "$d")")"
            fi
        done
        if [ -n "$ascii" ]; then echo "$ascii"; return 0; fi
    done
    return 1
}

# The touch controller can take a moment to come up after boot; retry a few times.
PID=""
for _ in 1 2 3 4 5; do
    if PID=$(read_product_id); then break; fi
    sleep 2
done

if [ -z "$PID" ]; then
    log "no Goodix touch controller found (panel not powered or not connected); leaving config.txt unchanged"
    exit 0
fi
log "touch controller product id: '$PID'"

case "$PID" in
    *9271*) PANEL=10inch ;;   # GT9271 -> 10.1"
    *911*)  PANEL=7inch  ;;   # GT911  -> 7"
    *)
        log "unrecognized product id '$PID'; leaving config.txt unchanged"
        exit 0
        ;;
esac

# 10.1" panel is 4-lane on Pi 5, 2-lane elsewhere; 7" is always 2-lane.
LANES=""
if [ "$PANEL" = "10inch" ] && [ "$pi_model" = "pi5" ]; then LANES=",4lane"; fi
DESIRED="dtoverlay=osoyoo-panel-dsi-${PANEL}${LANES}"

CURRENT=$(grep -E '^dtoverlay=osoyoo-panel-dsi-' "$CFG" | head -1)
log "panel=$PANEL  current='${CURRENT:-<none>}'  desired='$DESIRED'"

if [ "$CURRENT" = "$DESIRED" ]; then
    log "config.txt already matches the connected panel; no change"
    exit 0
fi

if [ "$DRYRUN" = "1" ]; then
    log "[dry-run] would set: $DESIRED  (and reboot)"
    exit 0
fi

cp -a "$CFG" "${CFG}.osoyoo.bak"
sed -i '/# OSOYOO DSI panel (added by install-direct.sh)/d; /^dtoverlay=osoyoo-panel-dsi-/d' "$CFG"
printf '\n# OSOYOO DSI panel (added by install-direct.sh)\n%s\n' "$DESIRED" >> "$CFG"
log "updated $CFG -> $DESIRED (backup ${CFG}.osoyoo.bak); rebooting to apply"
sync
systemctl reboot
