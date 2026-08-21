// SPDX-License-Identifier: GPL-2.0
/*
 * OSOYOO unified MIPI-DSI direct-connect panel driver.
 *
 * One driver for the whole OSOYOO direct-DSI panel line. Each panel is a
 * descriptor carrying its init sequence + display mode + DSI flags + a few
 * behaviour flags. Adding a new size = add a descriptor + its init table + an
 * overlay; no new code.
 *
 * Supported today:
 *   osoyoo,st7701s-3p5inch      3.5"  480x800   ST7701S   2-lane
 *   osoyoo,dsi-7inch            7"    720x1280  ILI9881C  2-lane
 *   osoyoo,dsi-10.1inch-2lane   10.1" 800x1280  ILI9881C  2-lane
 *   osoyoo,dsi-10.1inch-4lane   10.1" 800x1280  ILI9881C  4-lane
 *
 * Common hardware topology (all sizes): an STM32 companion MCU at i2c 0x45
 * (display_mcu, bound by osoyoo-panel-regulator) provides the panel/touch
 * reset gpio-controller AND the backlight PWM (register 0x03). The backlight is
 * registered HERE, in the panel driver, and stored in panel->backlight BEFORE
 * mipi_dsi_attach(). That is the sole requirement for the kernel to set
 * /sys/class/backlight/<dev>/display_name = the connector name ("DSI-1"), which
 * is what the desktop Screen Configuration keys off to show a native per-output
 * Brightness slider.
 *
 * Per-panel init encoding: every panel's sequence is a flat array of DCS
 * writes {len, data[], delay}. For the ILI9881C panels this expresses the
 * former page-switch/register-write helpers AND the old prepare() tail
 * (page 0 -> tear-on -> sleep-out -> display-on) as ordinary table entries, so
 * the DSI traffic is byte-for-byte identical to the previous osoyoo-panel-dsi
 * driver. The ST7701S issues display-on from enable() instead (after vc4 starts
 * video); that is selected with the OSOYOO_DISPLAY_ON_IN_ENABLE flag.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/i2c.h>
#include <linux/backlight.h>

#include <linux/gpio/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>

/*
 * Backlight PWM register on the STM32 companion MCU (display_mcu@45, the same
 * device that provides the reset gpio-controller). bit7 = enable,
 * bits[4:0] = brightness level 0..31.
 */
#define OSOYOO_MCU_REG_PWM	0x03
#define OSOYOO_MCU_PWM_ENABLE	0x80
#define OSOYOO_MCU_PWM_MAX	0x1f

/* Per-panel behaviour flags. */
#define OSOYOO_DISPLAY_ON_IN_ENABLE	BIT(0)	/* issue 0x29 in enable() (after
						 * video starts) instead of at the
						 * tail of the init table */

/* One DSI write of @len bytes (command + parameters), then sleep @delay ms. */
struct osoyoo_init_cmd {
	u8 len;
	u8 data[17];
	u16 delay;
};

#define OSOYOO_CMD(_delay, ...)						\
	{								\
		.len = sizeof((u8[]){ __VA_ARGS__ }),			\
		.data = { __VA_ARGS__ },				\
		.delay = (_delay),					\
	}

struct osoyoo_desc {
	const struct osoyoo_init_cmd *init;
	size_t init_length;
	const struct drm_display_mode *mode;
	const struct drm_display_mode *mode_rp1;	/* optional bcm2712/RP1 override */
	unsigned long mode_flags;
	unsigned int lanes;
	unsigned int reset_low_ms;	/* reset asserted (low) hold */
	unsigned int reset_high_ms;	/* reset released (high) settle before init */
	unsigned int flags;
};

struct osoyoo_panel {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	const struct osoyoo_desc *desc;
	struct gpio_desc *reset;
	struct i2c_client *mcu;		/* STM32 companion, for backlight PWM */
	enum drm_panel_orientation orientation;
};

static inline struct osoyoo_panel *to_osoyoo_panel(struct drm_panel *panel)
{
	return container_of(panel, struct osoyoo_panel, panel);
}

/* =========================== init sequences ============================== */

/*
 * 3.5" 480x800 ST7701S. Transcribed from "3.5 480X800_ST7701S.TXT".
 * 0xFF 0x77 0x01 0x00 0x00 0xNN selects command2 BKn. 0x11 (sleep-out) is kept
 * inline; 0x29 (display-on) is issued from enable() (OSOYOO_DISPLAY_ON_IN_ENABLE).
 */
static const struct osoyoo_init_cmd osoyoo_st7701s_3p5_init[] = {
	/* ---- enter command2 BK3 ---- */
	OSOYOO_CMD(0, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x13),
	OSOYOO_CMD(0, 0xEF, 0x08),

	/* ---- command2 BK0: LNESET/PORCTRL/INVSEL + Gamma ---- */
	OSOYOO_CMD(0, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x10),
	OSOYOO_CMD(0, 0xC0, 0x63, 0x00),
	OSOYOO_CMD(0, 0xC1, 0x10, 0x10),
	OSOYOO_CMD(0, 0xC2, 0x31, 0x02),
	OSOYOO_CMD(0, 0xCC, 0x10),
	OSOYOO_CMD(0, 0xB0, 0xC0, 0x0C, 0x92, 0x0C, 0x10, 0x05, 0x02, 0x0D,
			0x07, 0x21, 0x04, 0x53, 0x11, 0x6A, 0x32, 0x1F),
	OSOYOO_CMD(0, 0xB1, 0xC0, 0x87, 0xCF, 0x0C, 0x10, 0x06, 0x00, 0x03,
			0x08, 0x1D, 0x06, 0x54, 0x12, 0xE6, 0xEC, 0x0F),

	/* ---- command2 BK1: power / VCOM / GIP ---- */
	OSOYOO_CMD(0, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x11),
	OSOYOO_CMD(0, 0xB0, 0x5D),
	OSOYOO_CMD(0, 0xB1, 0x62),
	OSOYOO_CMD(0, 0xB2, 0x82),
	OSOYOO_CMD(0, 0xB3, 0x80),
	OSOYOO_CMD(0, 0xB5, 0x42),
	OSOYOO_CMD(0, 0xB7, 0x85),
	OSOYOO_CMD(0, 0xB8, 0x20),
	OSOYOO_CMD(0, 0xC0, 0x09),
	OSOYOO_CMD(0, 0xC1, 0x78),
	OSOYOO_CMD(0, 0xC2, 0x78),
	OSOYOO_CMD(0, 0xD0, 0x88),
	OSOYOO_CMD(100, 0xEE, 0x42),
	OSOYOO_CMD(0, 0xE0, 0x00, 0x00, 0x02),
	OSOYOO_CMD(0, 0xE1, 0x04, 0xA0, 0x06, 0xA0, 0x05, 0xA0, 0x07, 0xA0,
			0x00, 0x44, 0x44),
	OSOYOO_CMD(0, 0xE2, 0x00, 0x00, 0x33, 0x33, 0x01, 0xA0, 0x00, 0x00,
			0x01, 0xA0, 0x00, 0x00),
	OSOYOO_CMD(0, 0xE3, 0x00, 0x00, 0x33, 0x33),
	OSOYOO_CMD(0, 0xE4, 0x44, 0x44),
	OSOYOO_CMD(0, 0xE5, 0x0C, 0x30, 0xA0, 0xA0, 0x0E, 0x32, 0xA0, 0xA0,
			0x08, 0x2C, 0xA0, 0xA0, 0x0A, 0x2E, 0xA0, 0xA0),
	OSOYOO_CMD(0, 0xE6, 0x00, 0x00, 0x33, 0x33),
	OSOYOO_CMD(0, 0xE7, 0x44, 0x44),
	OSOYOO_CMD(0, 0xE8, 0x0D, 0x31, 0xA0, 0xA0, 0x0F, 0x33, 0xA0, 0xA0,
			0x09, 0x2D, 0xA0, 0xA0, 0x0B, 0x2F, 0xA0, 0xA0),
	OSOYOO_CMD(0, 0xEB, 0x00, 0x01, 0xE4, 0xE4, 0x44, 0x88, 0x00),
	OSOYOO_CMD(0, 0xED, 0xFF, 0xF5, 0x47, 0x6F, 0x0B, 0xA1, 0xA2, 0xBF,
			0xFB, 0x2A, 0x1A, 0xB0, 0xF6, 0x74, 0x5F, 0xFF),
	OSOYOO_CMD(0, 0xEF, 0x08, 0x08, 0x08, 0x40, 0x3F, 0x64),

	/* ---- command2 BK3 tweak ---- */
	OSOYOO_CMD(0, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x13),
	OSOYOO_CMD(0, 0xE8, 0x00, 0x0E),

	/* ---- return to user commands, sleep-out ---- */
	OSOYOO_CMD(0, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x00),
	OSOYOO_CMD(200, 0x11),

	/* ---- BK3 GIP power-on pulse ---- */
	OSOYOO_CMD(0, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x13),
	OSOYOO_CMD(10, 0xE8, 0x00, 0x0C),
	OSOYOO_CMD(0, 0xE8, 0x00, 0x00),

	/* ---- return to user commands ---- */
	OSOYOO_CMD(0, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x00),
	/* display-on (0x29) issued from enable() (OSOYOO_DISPLAY_ON_IN_ENABLE). */
};

/* 7" 720x1280 ILI9881C. Generated from the shipping osoyoo-panel-dsi tables. */
static const struct osoyoo_init_cmd osoyoo_dsi_7inch_init[] = {
	OSOYOO_CMD(0, 0xff, 0x98, 0x81, 0x03),
	OSOYOO_CMD(0, 0x01, 0x00),
	OSOYOO_CMD(0, 0x02, 0x00),
	OSOYOO_CMD(0, 0x03, 0x73),
	OSOYOO_CMD(0, 0x04, 0x00),
	OSOYOO_CMD(0, 0x05, 0x00),
	OSOYOO_CMD(0, 0x06, 0x0A),
	OSOYOO_CMD(0, 0x07, 0x00),
	OSOYOO_CMD(0, 0x08, 0x00),
	OSOYOO_CMD(0, 0x09, 0x00),
	OSOYOO_CMD(0, 0x0a, 0x00),
	OSOYOO_CMD(0, 0x0b, 0x00),
	OSOYOO_CMD(0, 0x0c, 0x01),
	OSOYOO_CMD(0, 0x0d, 0x00),
	OSOYOO_CMD(0, 0x0e, 0x00),
	OSOYOO_CMD(0, 0x0f, 0x17),
	OSOYOO_CMD(0, 0x10, 0x17),
	OSOYOO_CMD(0, 0x11, 0x00),
	OSOYOO_CMD(0, 0x12, 0x00),
	OSOYOO_CMD(0, 0x13, 0x00),
	OSOYOO_CMD(0, 0x14, 0x00),
	OSOYOO_CMD(0, 0x15, 0x00),
	OSOYOO_CMD(0, 0x16, 0x00),
	OSOYOO_CMD(0, 0x17, 0x00),
	OSOYOO_CMD(0, 0x18, 0x00),
	OSOYOO_CMD(0, 0x19, 0x00),
	OSOYOO_CMD(0, 0x1a, 0x00),
	OSOYOO_CMD(0, 0x1b, 0x00),
	OSOYOO_CMD(0, 0x1c, 0x00),
	OSOYOO_CMD(0, 0x1d, 0x00),
	OSOYOO_CMD(0, 0x1e, 0x40),
	OSOYOO_CMD(0, 0x1f, 0x80),
	OSOYOO_CMD(0, 0x20, 0x06),
	OSOYOO_CMD(0, 0x21, 0x01),
	OSOYOO_CMD(0, 0x22, 0x00),
	OSOYOO_CMD(0, 0x23, 0x00),
	OSOYOO_CMD(0, 0x24, 0x00),
	OSOYOO_CMD(0, 0x25, 0x00),
	OSOYOO_CMD(0, 0x26, 0x00),
	OSOYOO_CMD(0, 0x27, 0x00),
	OSOYOO_CMD(0, 0x28, 0x33),
	OSOYOO_CMD(0, 0x29, 0x03),
	OSOYOO_CMD(0, 0x2a, 0x00),
	OSOYOO_CMD(0, 0x2b, 0x00),
	OSOYOO_CMD(0, 0x2c, 0x00),
	OSOYOO_CMD(0, 0x2d, 0x00),
	OSOYOO_CMD(0, 0x2e, 0x00),
	OSOYOO_CMD(0, 0x2f, 0x00),
	OSOYOO_CMD(0, 0x30, 0x00),
	OSOYOO_CMD(0, 0x31, 0x00),
	OSOYOO_CMD(0, 0x32, 0x00),
	OSOYOO_CMD(0, 0x33, 0x00),
	OSOYOO_CMD(0, 0x34, 0x04),
	OSOYOO_CMD(0, 0x35, 0x00),
	OSOYOO_CMD(0, 0x36, 0x00),
	OSOYOO_CMD(0, 0x37, 0x00),
	OSOYOO_CMD(0, 0x38, 0x3C),
	OSOYOO_CMD(0, 0x39, 0x00),
	OSOYOO_CMD(0, 0x3a, 0x00),
	OSOYOO_CMD(0, 0x3b, 0x00),
	OSOYOO_CMD(0, 0x3c, 0x00),
	OSOYOO_CMD(0, 0x3d, 0x00),
	OSOYOO_CMD(0, 0x3e, 0x00),
	OSOYOO_CMD(0, 0x3f, 0x00),
	OSOYOO_CMD(0, 0x40, 0x00),
	OSOYOO_CMD(0, 0x41, 0x00),
	OSOYOO_CMD(0, 0x42, 0x00),
	OSOYOO_CMD(0, 0x43, 0x00),
	OSOYOO_CMD(0, 0x44, 0x00),
	OSOYOO_CMD(0, 0x50, 0x10),
	OSOYOO_CMD(0, 0x51, 0x32),
	OSOYOO_CMD(0, 0x52, 0x54),
	OSOYOO_CMD(0, 0x53, 0x76),
	OSOYOO_CMD(0, 0x54, 0x98),
	OSOYOO_CMD(0, 0x55, 0xba),
	OSOYOO_CMD(0, 0x56, 0x10),
	OSOYOO_CMD(0, 0x57, 0x32),
	OSOYOO_CMD(0, 0x58, 0x54),
	OSOYOO_CMD(0, 0x59, 0x76),
	OSOYOO_CMD(0, 0x5a, 0x98),
	OSOYOO_CMD(0, 0x5b, 0xba),
	OSOYOO_CMD(0, 0x5c, 0xdc),
	OSOYOO_CMD(0, 0x5d, 0xfe),
	OSOYOO_CMD(0, 0x5e, 0x00),
	OSOYOO_CMD(0, 0x5f, 0x0e),
	OSOYOO_CMD(0, 0x60, 0x0f),
	OSOYOO_CMD(0, 0x61, 0x0c),
	OSOYOO_CMD(0, 0x62, 0x0d),
	OSOYOO_CMD(0, 0x63, 0x06),
	OSOYOO_CMD(0, 0x64, 0x07),
	OSOYOO_CMD(0, 0x65, 0x02),
	OSOYOO_CMD(0, 0x66, 0x02),
	OSOYOO_CMD(0, 0x67, 0x02),
	OSOYOO_CMD(0, 0x68, 0x02),
	OSOYOO_CMD(0, 0x69, 0x01),
	OSOYOO_CMD(0, 0x6a, 0x00),
	OSOYOO_CMD(0, 0x6b, 0x02),
	OSOYOO_CMD(0, 0x6c, 0x15),
	OSOYOO_CMD(0, 0x6d, 0x14),
	OSOYOO_CMD(0, 0x6e, 0x02),
	OSOYOO_CMD(0, 0x6f, 0x02),
	OSOYOO_CMD(0, 0x70, 0x02),
	OSOYOO_CMD(0, 0x71, 0x02),
	OSOYOO_CMD(0, 0x72, 0x02),
	OSOYOO_CMD(0, 0x73, 0x02),
	OSOYOO_CMD(0, 0x74, 0x02),
	OSOYOO_CMD(0, 0x75, 0x0e),
	OSOYOO_CMD(0, 0x76, 0x0f),
	OSOYOO_CMD(0, 0x77, 0x0c),
	OSOYOO_CMD(0, 0x78, 0x0d),
	OSOYOO_CMD(0, 0x79, 0x06),
	OSOYOO_CMD(0, 0x7a, 0x07),
	OSOYOO_CMD(0, 0x7b, 0x02),
	OSOYOO_CMD(0, 0x7c, 0x02),
	OSOYOO_CMD(0, 0x7d, 0x02),
	OSOYOO_CMD(0, 0x7e, 0x02),
	OSOYOO_CMD(0, 0x7f, 0x01),
	OSOYOO_CMD(0, 0x80, 0x00),
	OSOYOO_CMD(0, 0x81, 0x02),
	OSOYOO_CMD(0, 0x82, 0x14),
	OSOYOO_CMD(0, 0x83, 0x15),
	OSOYOO_CMD(0, 0x84, 0x02),
	OSOYOO_CMD(0, 0x85, 0x02),
	OSOYOO_CMD(0, 0x86, 0x02),
	OSOYOO_CMD(0, 0x87, 0x02),
	OSOYOO_CMD(0, 0x88, 0x02),
	OSOYOO_CMD(0, 0x89, 0x02),
	OSOYOO_CMD(0, 0x8A, 0x02),
	OSOYOO_CMD(0, 0xff, 0x98, 0x81, 0x04),
	OSOYOO_CMD(0, 0x6C, 0x15),
	OSOYOO_CMD(0, 0x6E, 0x2A),
	OSOYOO_CMD(0, 0x6F, 0x37),
	OSOYOO_CMD(0, 0x3B, 0x98),
	OSOYOO_CMD(0, 0x3a, 0x94),
	OSOYOO_CMD(0, 0x8D, 0x1F),
	OSOYOO_CMD(0, 0x87, 0xBA),
	OSOYOO_CMD(0, 0x26, 0x76),
	OSOYOO_CMD(0, 0xB2, 0xD1),
	OSOYOO_CMD(0, 0xB5, 0x06),
	OSOYOO_CMD(0, 0x38, 0x01),
	OSOYOO_CMD(0, 0x39, 0x00),
	OSOYOO_CMD(0, 0xff, 0x98, 0x81, 0x01),
	OSOYOO_CMD(0, 0xB7, 0x03),
	OSOYOO_CMD(0, 0x22, 0x0A),
	OSOYOO_CMD(0, 0x2E, 0xC8),
	OSOYOO_CMD(0, 0x31, 0x00),
	OSOYOO_CMD(0, 0x53, 0x5d),
	OSOYOO_CMD(0, 0x55, 0x5d),
	OSOYOO_CMD(0, 0x40, 0x33),
	OSOYOO_CMD(0, 0x50, 0x85),
	OSOYOO_CMD(0, 0x51, 0x85),
	OSOYOO_CMD(0, 0x60, 0x26),
	OSOYOO_CMD(0, 0xA0, 0x08),
	OSOYOO_CMD(0, 0xA1, 0x0B),
	OSOYOO_CMD(0, 0xA2, 0x18),
	OSOYOO_CMD(0, 0xA3, 0x15),
	OSOYOO_CMD(0, 0xA4, 0x16),
	OSOYOO_CMD(0, 0xA5, 0x29),
	OSOYOO_CMD(0, 0xA6, 0x1E),
	OSOYOO_CMD(0, 0xA7, 0x1E),
	OSOYOO_CMD(0, 0xA8, 0x57),
	OSOYOO_CMD(0, 0xA9, 0x1C),
	OSOYOO_CMD(0, 0xAA, 0x2A),
	OSOYOO_CMD(0, 0xAB, 0x43),
	OSOYOO_CMD(0, 0xAC, 0x1F),
	OSOYOO_CMD(0, 0xAD, 0x20),
	OSOYOO_CMD(0, 0xAE, 0x52),
	OSOYOO_CMD(0, 0xAF, 0x2A),
	OSOYOO_CMD(0, 0xB0, 0x30),
	OSOYOO_CMD(0, 0xB1, 0x32),
	OSOYOO_CMD(0, 0xB2, 0x60),
	OSOYOO_CMD(0, 0xB3, 0x39),
	OSOYOO_CMD(0, 0xC0, 0x08),
	OSOYOO_CMD(0, 0xC1, 0x24),
	OSOYOO_CMD(0, 0xC2, 0x2E),
	OSOYOO_CMD(0, 0xC3, 0x0D),
	OSOYOO_CMD(0, 0xC4, 0x12),
	OSOYOO_CMD(0, 0xC5, 0x23),
	OSOYOO_CMD(0, 0xC6, 0x18),
	OSOYOO_CMD(0, 0xC7, 0x1B),
	OSOYOO_CMD(0, 0xC8, 0x85),
	OSOYOO_CMD(0, 0xC9, 0x1B),
	OSOYOO_CMD(0, 0xCA, 0x27),
	OSOYOO_CMD(0, 0xCB, 0x75),
	OSOYOO_CMD(0, 0xCC, 0x1a),
	OSOYOO_CMD(0, 0xCD, 0x18),
	OSOYOO_CMD(0, 0xCE, 0x50),
	OSOYOO_CMD(0, 0xCF, 0x22),
	OSOYOO_CMD(0, 0xD0, 0x22),
	OSOYOO_CMD(0, 0xD1, 0x50),
	OSOYOO_CMD(0, 0xD2, 0x67),
	OSOYOO_CMD(0, 0xD3, 0x39),
	/* ---- return to user page, tear-on, sleep-out(120ms), display-on (was prepare tail) ---- */
	OSOYOO_CMD(0, 0xff, 0x98, 0x81, 0x00),
	OSOYOO_CMD(0, 0x35, 0x00),
	OSOYOO_CMD(120, 0x11),
	OSOYOO_CMD(0, 0x29)
};

/* 10.1" 800x1280 ILI9881C, 2-lane. Generated from the shipping tables. */
static const struct osoyoo_init_cmd osoyoo_dsi_10inch_2lane_init[] = {
	OSOYOO_CMD(0, 0xff, 0x98, 0x81, 0x03),
	OSOYOO_CMD(0, 0x01, 0x00),
	OSOYOO_CMD(0, 0x02, 0x00),
	OSOYOO_CMD(0, 0x03, 0x53),
	OSOYOO_CMD(0, 0x04, 0xD3),
	OSOYOO_CMD(0, 0x05, 0x00),
	OSOYOO_CMD(0, 0x06, 0x0D),
	OSOYOO_CMD(0, 0x07, 0x08),
	OSOYOO_CMD(0, 0x08, 0x00),
	OSOYOO_CMD(0, 0x09, 0x00),
	OSOYOO_CMD(0, 0x0a, 0x00),
	OSOYOO_CMD(0, 0x0b, 0x00),
	OSOYOO_CMD(0, 0x0c, 0x00),
	OSOYOO_CMD(0, 0x0d, 0x00),
	OSOYOO_CMD(0, 0x0e, 0x00),
	OSOYOO_CMD(0, 0x0f, 0x28),
	OSOYOO_CMD(0, 0x10, 0x28),
	OSOYOO_CMD(0, 0x11, 0x00),
	OSOYOO_CMD(0, 0x12, 0x00),
	OSOYOO_CMD(0, 0x13, 0x00),
	OSOYOO_CMD(0, 0x14, 0x00),
	OSOYOO_CMD(0, 0x15, 0x00),
	OSOYOO_CMD(0, 0x16, 0x00),
	OSOYOO_CMD(0, 0x17, 0x00),
	OSOYOO_CMD(0, 0x18, 0x00),
	OSOYOO_CMD(0, 0x19, 0x00),
	OSOYOO_CMD(0, 0x1a, 0x00),
	OSOYOO_CMD(0, 0x1b, 0x00),
	OSOYOO_CMD(0, 0x1c, 0x00),
	OSOYOO_CMD(0, 0x1d, 0x00),
	OSOYOO_CMD(0, 0x1e, 0x40),
	OSOYOO_CMD(0, 0x1f, 0x80),
	OSOYOO_CMD(0, 0x20, 0x06),
	OSOYOO_CMD(0, 0x21, 0x01),
	OSOYOO_CMD(0, 0x22, 0x00),
	OSOYOO_CMD(0, 0x23, 0x00),
	OSOYOO_CMD(0, 0x24, 0x00),
	OSOYOO_CMD(0, 0x25, 0x00),
	OSOYOO_CMD(0, 0x26, 0x00),
	OSOYOO_CMD(0, 0x27, 0x00),
	OSOYOO_CMD(0, 0x28, 0x33),
	OSOYOO_CMD(0, 0x29, 0x33),
	OSOYOO_CMD(0, 0x2a, 0x00),
	OSOYOO_CMD(0, 0x2b, 0x00),
	OSOYOO_CMD(0, 0x2c, 0x00),
	OSOYOO_CMD(0, 0x2d, 0x00),
	OSOYOO_CMD(0, 0x2e, 0x00),
	OSOYOO_CMD(0, 0x2f, 0x00),
	OSOYOO_CMD(0, 0x30, 0x00),
	OSOYOO_CMD(0, 0x31, 0x00),
	OSOYOO_CMD(0, 0x32, 0x00),
	OSOYOO_CMD(0, 0x33, 0x00),
	OSOYOO_CMD(0, 0x34, 0x03),
	OSOYOO_CMD(0, 0x35, 0x00),
	OSOYOO_CMD(0, 0x36, 0x00),
	OSOYOO_CMD(0, 0x37, 0x00),
	OSOYOO_CMD(0, 0x38, 0x96),
	OSOYOO_CMD(0, 0x39, 0x00),
	OSOYOO_CMD(0, 0x3a, 0x00),
	OSOYOO_CMD(0, 0x3b, 0x00),
	OSOYOO_CMD(0, 0x3c, 0x00),
	OSOYOO_CMD(0, 0x3d, 0x00),
	OSOYOO_CMD(0, 0x3e, 0x00),
	OSOYOO_CMD(0, 0x3f, 0x00),
	OSOYOO_CMD(0, 0x40, 0x00),
	OSOYOO_CMD(0, 0x41, 0x00),
	OSOYOO_CMD(0, 0x42, 0x00),
	OSOYOO_CMD(0, 0x43, 0x00),
	OSOYOO_CMD(0, 0x44, 0x00),
	OSOYOO_CMD(0, 0x50, 0x00),
	OSOYOO_CMD(0, 0x51, 0x23),
	OSOYOO_CMD(0, 0x52, 0x45),
	OSOYOO_CMD(0, 0x53, 0x67),
	OSOYOO_CMD(0, 0x54, 0x89),
	OSOYOO_CMD(0, 0x55, 0xAB),
	OSOYOO_CMD(0, 0x56, 0x01),
	OSOYOO_CMD(0, 0x57, 0x23),
	OSOYOO_CMD(0, 0x58, 0x45),
	OSOYOO_CMD(0, 0x59, 0x67),
	OSOYOO_CMD(0, 0x5a, 0x89),
	OSOYOO_CMD(0, 0x5b, 0xAB),
	OSOYOO_CMD(0, 0x5c, 0xCD),
	OSOYOO_CMD(0, 0x5d, 0xEF),
	OSOYOO_CMD(0, 0x5e, 0x00),
	OSOYOO_CMD(0, 0x5f, 0x08),
	OSOYOO_CMD(0, 0x60, 0x08),
	OSOYOO_CMD(0, 0x61, 0x06),
	OSOYOO_CMD(0, 0x62, 0x06),
	OSOYOO_CMD(0, 0x63, 0x01),
	OSOYOO_CMD(0, 0x64, 0x01),
	OSOYOO_CMD(0, 0x65, 0x00),
	OSOYOO_CMD(0, 0x66, 0x00),
	OSOYOO_CMD(0, 0x67, 0x02),
	OSOYOO_CMD(0, 0x68, 0x15),
	OSOYOO_CMD(0, 0x69, 0x15),
	OSOYOO_CMD(0, 0x6a, 0x14),
	OSOYOO_CMD(0, 0x6b, 0x14),
	OSOYOO_CMD(0, 0x6c, 0x0D),
	OSOYOO_CMD(0, 0x6d, 0x0D),
	OSOYOO_CMD(0, 0x6e, 0x0C),
	OSOYOO_CMD(0, 0x6f, 0x0C),
	OSOYOO_CMD(0, 0x70, 0x0F),
	OSOYOO_CMD(0, 0x71, 0x0F),
	OSOYOO_CMD(0, 0x72, 0x0E),
	OSOYOO_CMD(0, 0x73, 0x0E),
	OSOYOO_CMD(0, 0x74, 0x02),
	OSOYOO_CMD(0, 0x75, 0x08),
	OSOYOO_CMD(0, 0x76, 0x08),
	OSOYOO_CMD(0, 0x77, 0x06),
	OSOYOO_CMD(0, 0x78, 0x06),
	OSOYOO_CMD(0, 0x79, 0x01),
	OSOYOO_CMD(0, 0x7a, 0x01),
	OSOYOO_CMD(0, 0x7b, 0x00),
	OSOYOO_CMD(0, 0x7c, 0x00),
	OSOYOO_CMD(0, 0x7d, 0x02),
	OSOYOO_CMD(0, 0x7e, 0x15),
	OSOYOO_CMD(0, 0x7f, 0x15),
	OSOYOO_CMD(0, 0x80, 0x14),
	OSOYOO_CMD(0, 0x81, 0x14),
	OSOYOO_CMD(0, 0x82, 0x0D),
	OSOYOO_CMD(0, 0x83, 0x0D),
	OSOYOO_CMD(0, 0x84, 0x0C),
	OSOYOO_CMD(0, 0x85, 0x0C),
	OSOYOO_CMD(0, 0x86, 0x0F),
	OSOYOO_CMD(0, 0x87, 0x0F),
	OSOYOO_CMD(0, 0x88, 0x0E),
	OSOYOO_CMD(0, 0x89, 0x0E),
	OSOYOO_CMD(0, 0x8A, 0x02),
	OSOYOO_CMD(0, 0xff, 0x98, 0x81, 0x04),
	OSOYOO_CMD(0, 0x6E, 0x2B),
	OSOYOO_CMD(0, 0x6F, 0x37),
	OSOYOO_CMD(0, 0x3A, 0xA4),
	OSOYOO_CMD(0, 0x8D, 0x1A),
	OSOYOO_CMD(0, 0x87, 0xBA),
	OSOYOO_CMD(0, 0xB2, 0xD1),
	OSOYOO_CMD(0, 0x88, 0x0B),
	OSOYOO_CMD(0, 0x38, 0x01),
	OSOYOO_CMD(0, 0x39, 0x00),
	OSOYOO_CMD(0, 0xB5, 0x07),
	OSOYOO_CMD(0, 0x31, 0x75),
	OSOYOO_CMD(0, 0x3B, 0x98),
	OSOYOO_CMD(0, 0xff, 0x98, 0x81, 0x01),
	OSOYOO_CMD(0, 0xB7, 0x03),
	OSOYOO_CMD(0, 0x22, 0x0A),
	OSOYOO_CMD(0, 0x31, 0x00),
	OSOYOO_CMD(0, 0x53, 0x40),
	OSOYOO_CMD(0, 0x55, 0x40),
	OSOYOO_CMD(0, 0x50, 0x99),
	OSOYOO_CMD(0, 0x51, 0x94),
	OSOYOO_CMD(0, 0x60, 0x10),
	OSOYOO_CMD(0, 0x62, 0x20),
	OSOYOO_CMD(0, 0xA0, 0x00),
	OSOYOO_CMD(0, 0xA1, 0x00),
	OSOYOO_CMD(0, 0xA2, 0x15),
	OSOYOO_CMD(0, 0xA3, 0x14),
	OSOYOO_CMD(0, 0xA5, 0x2F),
	OSOYOO_CMD(0, 0xA6, 0x25),
	OSOYOO_CMD(0, 0xA7, 0x24),
	OSOYOO_CMD(0, 0xA8, 0x80),
	OSOYOO_CMD(0, 0xA9, 0x1F),
	OSOYOO_CMD(0, 0xAA, 0x2C),
	OSOYOO_CMD(0, 0xAB, 0x6C),
	OSOYOO_CMD(0, 0xAC, 0x16),
	OSOYOO_CMD(0, 0xAD, 0x14),
	OSOYOO_CMD(0, 0xAE, 0x4D),
	OSOYOO_CMD(0, 0xAF, 0x20),
	OSOYOO_CMD(0, 0xB0, 0x29),
	OSOYOO_CMD(0, 0xB1, 0x4F),
	OSOYOO_CMD(0, 0xB2, 0x5F),
	OSOYOO_CMD(0, 0xB3, 0x23),
	OSOYOO_CMD(0, 0xC0, 0x00),
	OSOYOO_CMD(0, 0xC1, 0x2E),
	OSOYOO_CMD(0, 0xC2, 0x3B),
	OSOYOO_CMD(0, 0xC3, 0x15),
	OSOYOO_CMD(0, 0xC4, 0x16),
	OSOYOO_CMD(0, 0xC5, 0x28),
	OSOYOO_CMD(0, 0xC6, 0x1A),
	OSOYOO_CMD(0, 0xC7, 0x1C),
	OSOYOO_CMD(0, 0xC8, 0xA7),
	OSOYOO_CMD(0, 0xC9, 0x1B),
	OSOYOO_CMD(0, 0xCA, 0x28),
	OSOYOO_CMD(0, 0xCB, 0x92),
	OSOYOO_CMD(0, 0xCC, 0x1F),
	OSOYOO_CMD(0, 0xCD, 0x1C),
	OSOYOO_CMD(0, 0xCE, 0x4B),
	OSOYOO_CMD(0, 0xCF, 0x1F),
	OSOYOO_CMD(0, 0xD0, 0x28),
	OSOYOO_CMD(0, 0xD1, 0x4E),
	OSOYOO_CMD(0, 0xD2, 0x5C),
	OSOYOO_CMD(0, 0xD3, 0x23),
	/* ---- return to user page, tear-on, sleep-out(120ms), display-on (was prepare tail) ---- */
	OSOYOO_CMD(0, 0xff, 0x98, 0x81, 0x00),
	OSOYOO_CMD(0, 0x35, 0x00),
	OSOYOO_CMD(120, 0x11),
	OSOYOO_CMD(0, 0x29)
};

/* 10.1" 800x1280 ILI9881C, 4-lane. Generated from the shipping tables. */
static const struct osoyoo_init_cmd osoyoo_dsi_10inch_4lane_init[] = {
	OSOYOO_CMD(0, 0xff, 0x98, 0x81, 0x03),
	OSOYOO_CMD(0, 0x01, 0x00),
	OSOYOO_CMD(0, 0x02, 0x00),
	OSOYOO_CMD(0, 0x03, 0x53),
	OSOYOO_CMD(0, 0x04, 0xD3),
	OSOYOO_CMD(0, 0x05, 0x00),
	OSOYOO_CMD(0, 0x06, 0x0D),
	OSOYOO_CMD(0, 0x07, 0x08),
	OSOYOO_CMD(0, 0x08, 0x00),
	OSOYOO_CMD(0, 0x09, 0x00),
	OSOYOO_CMD(0, 0x0a, 0x00),
	OSOYOO_CMD(0, 0x0b, 0x00),
	OSOYOO_CMD(0, 0x0c, 0x00),
	OSOYOO_CMD(0, 0x0d, 0x00),
	OSOYOO_CMD(0, 0x0e, 0x00),
	OSOYOO_CMD(0, 0x0f, 0x28),
	OSOYOO_CMD(0, 0x10, 0x28),
	OSOYOO_CMD(0, 0x11, 0x00),
	OSOYOO_CMD(0, 0x12, 0x00),
	OSOYOO_CMD(0, 0x13, 0x00),
	OSOYOO_CMD(0, 0x14, 0x00),
	OSOYOO_CMD(0, 0x15, 0x00),
	OSOYOO_CMD(0, 0x16, 0x00),
	OSOYOO_CMD(0, 0x17, 0x00),
	OSOYOO_CMD(0, 0x18, 0x00),
	OSOYOO_CMD(0, 0x19, 0x00),
	OSOYOO_CMD(0, 0x1a, 0x00),
	OSOYOO_CMD(0, 0x1b, 0x00),
	OSOYOO_CMD(0, 0x1c, 0x00),
	OSOYOO_CMD(0, 0x1d, 0x00),
	OSOYOO_CMD(0, 0x1e, 0x40),
	OSOYOO_CMD(0, 0x1f, 0x80),
	OSOYOO_CMD(0, 0x20, 0x06),
	OSOYOO_CMD(0, 0x21, 0x01),
	OSOYOO_CMD(0, 0x22, 0x00),
	OSOYOO_CMD(0, 0x23, 0x00),
	OSOYOO_CMD(0, 0x24, 0x00),
	OSOYOO_CMD(0, 0x25, 0x00),
	OSOYOO_CMD(0, 0x26, 0x00),
	OSOYOO_CMD(0, 0x27, 0x00),
	OSOYOO_CMD(0, 0x28, 0x33),
	OSOYOO_CMD(0, 0x29, 0x33),
	OSOYOO_CMD(0, 0x2a, 0x00),
	OSOYOO_CMD(0, 0x2b, 0x00),
	OSOYOO_CMD(0, 0x2c, 0x00),
	OSOYOO_CMD(0, 0x2d, 0x00),
	OSOYOO_CMD(0, 0x2e, 0x00),
	OSOYOO_CMD(0, 0x2f, 0x00),
	OSOYOO_CMD(0, 0x30, 0x00),
	OSOYOO_CMD(0, 0x31, 0x00),
	OSOYOO_CMD(0, 0x32, 0x00),
	OSOYOO_CMD(0, 0x33, 0x00),
	OSOYOO_CMD(0, 0x34, 0x03),
	OSOYOO_CMD(0, 0x35, 0x00),
	OSOYOO_CMD(0, 0x36, 0x00),
	OSOYOO_CMD(0, 0x37, 0x00),
	OSOYOO_CMD(0, 0x38, 0x96),
	OSOYOO_CMD(0, 0x39, 0x00),
	OSOYOO_CMD(0, 0x3a, 0x00),
	OSOYOO_CMD(0, 0x3b, 0x00),
	OSOYOO_CMD(0, 0x3c, 0x00),
	OSOYOO_CMD(0, 0x3d, 0x00),
	OSOYOO_CMD(0, 0x3e, 0x00),
	OSOYOO_CMD(0, 0x3f, 0x00),
	OSOYOO_CMD(0, 0x40, 0x00),
	OSOYOO_CMD(0, 0x41, 0x00),
	OSOYOO_CMD(0, 0x42, 0x00),
	OSOYOO_CMD(0, 0x43, 0x00),
	OSOYOO_CMD(0, 0x44, 0x00),
	OSOYOO_CMD(0, 0x50, 0x00),
	OSOYOO_CMD(0, 0x51, 0x23),
	OSOYOO_CMD(0, 0x52, 0x45),
	OSOYOO_CMD(0, 0x53, 0x67),
	OSOYOO_CMD(0, 0x54, 0x89),
	OSOYOO_CMD(0, 0x55, 0xAB),
	OSOYOO_CMD(0, 0x56, 0x01),
	OSOYOO_CMD(0, 0x57, 0x23),
	OSOYOO_CMD(0, 0x58, 0x45),
	OSOYOO_CMD(0, 0x59, 0x67),
	OSOYOO_CMD(0, 0x5a, 0x89),
	OSOYOO_CMD(0, 0x5b, 0xAB),
	OSOYOO_CMD(0, 0x5c, 0xCD),
	OSOYOO_CMD(0, 0x5d, 0xEF),
	OSOYOO_CMD(0, 0x5e, 0x00),
	OSOYOO_CMD(0, 0x5f, 0x08),
	OSOYOO_CMD(0, 0x60, 0x08),
	OSOYOO_CMD(0, 0x61, 0x06),
	OSOYOO_CMD(0, 0x62, 0x06),
	OSOYOO_CMD(0, 0x63, 0x01),
	OSOYOO_CMD(0, 0x64, 0x01),
	OSOYOO_CMD(0, 0x65, 0x00),
	OSOYOO_CMD(0, 0x66, 0x00),
	OSOYOO_CMD(0, 0x67, 0x02),
	OSOYOO_CMD(0, 0x68, 0x15),
	OSOYOO_CMD(0, 0x69, 0x15),
	OSOYOO_CMD(0, 0x6a, 0x14),
	OSOYOO_CMD(0, 0x6b, 0x14),
	OSOYOO_CMD(0, 0x6c, 0x0D),
	OSOYOO_CMD(0, 0x6d, 0x0D),
	OSOYOO_CMD(0, 0x6e, 0x0C),
	OSOYOO_CMD(0, 0x6f, 0x0C),
	OSOYOO_CMD(0, 0x70, 0x0F),
	OSOYOO_CMD(0, 0x71, 0x0F),
	OSOYOO_CMD(0, 0x72, 0x0E),
	OSOYOO_CMD(0, 0x73, 0x0E),
	OSOYOO_CMD(0, 0x74, 0x02),
	OSOYOO_CMD(0, 0x75, 0x08),
	OSOYOO_CMD(0, 0x76, 0x08),
	OSOYOO_CMD(0, 0x77, 0x06),
	OSOYOO_CMD(0, 0x78, 0x06),
	OSOYOO_CMD(0, 0x79, 0x01),
	OSOYOO_CMD(0, 0x7a, 0x01),
	OSOYOO_CMD(0, 0x7b, 0x00),
	OSOYOO_CMD(0, 0x7c, 0x00),
	OSOYOO_CMD(0, 0x7d, 0x02),
	OSOYOO_CMD(0, 0x7e, 0x15),
	OSOYOO_CMD(0, 0x7f, 0x15),
	OSOYOO_CMD(0, 0x80, 0x14),
	OSOYOO_CMD(0, 0x81, 0x14),
	OSOYOO_CMD(0, 0x82, 0x0D),
	OSOYOO_CMD(0, 0x83, 0x0D),
	OSOYOO_CMD(0, 0x84, 0x0C),
	OSOYOO_CMD(0, 0x85, 0x0C),
	OSOYOO_CMD(0, 0x86, 0x0F),
	OSOYOO_CMD(0, 0x87, 0x0F),
	OSOYOO_CMD(0, 0x88, 0x0E),
	OSOYOO_CMD(0, 0x89, 0x0E),
	OSOYOO_CMD(0, 0x8A, 0x02),
	OSOYOO_CMD(0, 0xff, 0x98, 0x81, 0x04),
	OSOYOO_CMD(0, 0x6E, 0x2B),
	OSOYOO_CMD(0, 0x6F, 0x37),
	OSOYOO_CMD(0, 0x3A, 0xA4),
	OSOYOO_CMD(0, 0x8D, 0x1A),
	OSOYOO_CMD(0, 0x87, 0xBA),
	OSOYOO_CMD(0, 0xB2, 0xD1),
	OSOYOO_CMD(0, 0x88, 0x0B),
	OSOYOO_CMD(0, 0x38, 0x01),
	OSOYOO_CMD(0, 0x39, 0x00),
	OSOYOO_CMD(0, 0xB5, 0x07),
	OSOYOO_CMD(0, 0x31, 0x75),
	OSOYOO_CMD(0, 0x3B, 0x98),
	OSOYOO_CMD(0, 0xff, 0x98, 0x81, 0x01),
	OSOYOO_CMD(0, 0x22, 0x0A),
	OSOYOO_CMD(0, 0x31, 0x00),
	OSOYOO_CMD(0, 0x53, 0x40),
	OSOYOO_CMD(0, 0x55, 0x40),
	OSOYOO_CMD(0, 0x50, 0x99),
	OSOYOO_CMD(0, 0x51, 0x94),
	OSOYOO_CMD(0, 0x60, 0x10),
	OSOYOO_CMD(0, 0x62, 0x20),
	OSOYOO_CMD(0, 0xA0, 0x00),
	OSOYOO_CMD(0, 0xA1, 0x00),
	OSOYOO_CMD(0, 0xA2, 0x15),
	OSOYOO_CMD(0, 0xA3, 0x14),
	OSOYOO_CMD(0, 0xA5, 0x2F),
	OSOYOO_CMD(0, 0xA6, 0x25),
	OSOYOO_CMD(0, 0xA7, 0x24),
	OSOYOO_CMD(0, 0xA8, 0x80),
	OSOYOO_CMD(0, 0xA9, 0x1F),
	OSOYOO_CMD(0, 0xAA, 0x2C),
	OSOYOO_CMD(0, 0xAB, 0x6C),
	OSOYOO_CMD(0, 0xAC, 0x16),
	OSOYOO_CMD(0, 0xAD, 0x14),
	OSOYOO_CMD(0, 0xAE, 0x4D),
	OSOYOO_CMD(0, 0xAF, 0x20),
	OSOYOO_CMD(0, 0xB0, 0x29),
	OSOYOO_CMD(0, 0xB1, 0x4F),
	OSOYOO_CMD(0, 0xB2, 0x5F),
	OSOYOO_CMD(0, 0xB3, 0x23),
	OSOYOO_CMD(0, 0xC0, 0x00),
	OSOYOO_CMD(0, 0xC1, 0x2E),
	OSOYOO_CMD(0, 0xC2, 0x3B),
	OSOYOO_CMD(0, 0xC3, 0x15),
	OSOYOO_CMD(0, 0xC4, 0x16),
	OSOYOO_CMD(0, 0xC5, 0x28),
	OSOYOO_CMD(0, 0xC6, 0x1A),
	OSOYOO_CMD(0, 0xC7, 0x1C),
	OSOYOO_CMD(0, 0xC8, 0xA7),
	OSOYOO_CMD(0, 0xC9, 0x1B),
	OSOYOO_CMD(0, 0xCA, 0x28),
	OSOYOO_CMD(0, 0xCB, 0x92),
	OSOYOO_CMD(0, 0xCC, 0x1F),
	OSOYOO_CMD(0, 0xCD, 0x1C),
	OSOYOO_CMD(0, 0xCE, 0x4B),
	OSOYOO_CMD(0, 0xCF, 0x1F),
	OSOYOO_CMD(0, 0xD0, 0x28),
	OSOYOO_CMD(0, 0xD1, 0x4E),
	OSOYOO_CMD(0, 0xD2, 0x5C),
	OSOYOO_CMD(0, 0xD3, 0x23),
	/* ---- return to user page, tear-on, sleep-out(120ms), display-on (was prepare tail) ---- */
	OSOYOO_CMD(0, 0xff, 0x98, 0x81, 0x00),
	OSOYOO_CMD(0, 0x35, 0x00),
	OSOYOO_CMD(120, 0x11),
	OSOYOO_CMD(0, 0x29)
};

/* ============================== modes ==================================== */

static const struct drm_display_mode osoyoo_st7701s_3p5_mode = {
	/* Proven-working LT2911R timing: HFP16/HSA32/HBP32, VFP131/VSA2/VBP45. */
	.clock		= 30000,

	.hdisplay	= 480,
	.hsync_start	= 480 + 16,
	.hsync_end	= 480 + 16 + 32,
	.htotal		= 480 + 16 + 32 + 32,

	.vdisplay	= 800,
	.vsync_start	= 800 + 131,
	.vsync_end	= 800 + 131 + 2,
	.vtotal		= 800 + 131 + 2 + 45,

	.width_mm	= 45,
	.height_mm	= 76,
};

/*
 * Pi 5 (RP1) variant of the 3.5" mode. Selected automatically on bcm2712 in
 * osoyoo_panel_get_modes(). This was previously raised to 40 MHz (480 Mbps/lane)
 * on the assumption that RP1's DSI D-PHY could not lock the ~360 Mbps/lane link
 * from the 30 MHz pixel clock, but 40 MHz caused severe flicker and horizontal
 * stripes on tested units. Reverted to the same 30 MHz timing as the default
 * mode, which is verified stable on Pi 5.
 */
static const struct drm_display_mode osoyoo_st7701s_3p5_mode_rp1 = {
	.clock		= 30000,

	.hdisplay	= 480,
	.hsync_start	= 480 + 16,
	.hsync_end	= 480 + 16 + 32,
	.htotal		= 480 + 16 + 32 + 32,

	.vdisplay	= 800,
	.vsync_start	= 800 + 131,
	.vsync_end	= 800 + 131 + 2,
	.vtotal		= 800 + 131 + 2 + 45,

	.width_mm	= 45,
	.height_mm	= 76,
};

static const struct drm_display_mode osoyoo_dsi_7inch_mode = {
	.clock		= 51200,

	.hdisplay	= 720,
	.hsync_start	= 720 + 20,
	.hsync_end	= 720 + 20 + 6,
	.htotal		= 720 + 20 + 6 + 10,

	.vdisplay	= 1280,
	.vsync_start	= 1280 + 50,
	.vsync_end	= 1280 + 50 + 6,
	.vtotal		= 1280 + 50 + 6 + 20,

	.width_mm	= 90,
	.height_mm	= 151,
};

static const struct drm_display_mode osoyoo_dsi_10inch_2lane_mode = {
	.clock		= 74673,

	.hdisplay	= 800,
	.hsync_start	= 800 + 60,
	.hsync_end	= 800 + 60 + 20,
	.htotal		= 800 + 60 + 20 + 60,

	.vdisplay	= 1280,
	.vsync_start	= 1280 + 16,
	.vsync_end	= 1280 + 16 + 6,
	.vtotal		= 1280 + 16 + 6 + 22,

	.width_mm	= 135,
	.height_mm	= 216,
};

static const struct drm_display_mode osoyoo_dsi_10inch_4lane_mode = {
	.clock		= 74673,

	.hdisplay	= 800,
	.hsync_start	= 800 + 60,
	.hsync_end	= 800 + 60 + 20,
	.htotal		= 800 + 60 + 20 + 60,

	.vdisplay	= 1280,
	.vsync_start	= 1280 + 16,
	.vsync_end	= 1280 + 16 + 6,
	.vtotal		= 1280 + 16 + 6 + 22,

	.width_mm	= 135,
	.height_mm	= 216,
};

/* ============================= backlight ================================= */

static int osoyoo_bl_update_status(struct backlight_device *bl)
{
	struct osoyoo_panel *ctx = bl_get_data(bl);
	int brightness = backlight_get_brightness(bl);
	u8 val = brightness ?
		 (OSOYOO_MCU_PWM_ENABLE | (brightness & OSOYOO_MCU_PWM_MAX)) : 0;

	return i2c_smbus_write_byte_data(ctx->mcu, OSOYOO_MCU_REG_PWM, val);
}

static const struct backlight_ops osoyoo_bl_ops = {
	.update_status = osoyoo_bl_update_status,
};

/*
 * Register the backlight in the panel driver itself and store it in
 * panel->backlight BEFORE drm_panel_add()/mipi_dsi_attach(), so
 * panel_bridge_attach() copies the connector name ("DSI-1") into
 * /sys/class/backlight/<dev>/display_name and the desktop shows a slider.
 */
static int osoyoo_backlight_register(struct osoyoo_panel *ctx)
{
	struct device *dev = &ctx->dsi->dev;
	struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.max_brightness = OSOYOO_MCU_PWM_MAX,
		.brightness = OSOYOO_MCU_PWM_MAX,
	};
	struct backlight_device *bl;
	struct device_node *mcu_np;

	/* The STM32 that owns the PWM register is the reset-gpio provider node. */
	mcu_np = of_parse_phandle(dev->of_node, "reset-gpio", 0);
	if (!mcu_np)
		mcu_np = of_parse_phandle(dev->of_node, "reset-gpios", 0);
	if (!mcu_np)
		return dev_err_probe(dev, -EINVAL,
				     "no reset-gpio phandle to locate backlight MCU\n");

	ctx->mcu = of_find_i2c_device_by_node(mcu_np);
	of_node_put(mcu_np);
	if (!ctx->mcu)
		return -EPROBE_DEFER;	/* STM32 i2c device not instantiated yet */

	bl = devm_backlight_device_register(dev, dev_name(dev), dev, ctx,
					    &osoyoo_bl_ops, &props);
	if (IS_ERR(bl)) {
		put_device(&ctx->mcu->dev);
		ctx->mcu = NULL;
		return PTR_ERR(bl);
	}

	ctx->panel.backlight = bl;
	return 0;
}

/* ============================ panel funcs =============================== */

static int osoyoo_panel_prepare(struct drm_panel *panel)
{
	struct osoyoo_panel *ctx = to_osoyoo_panel(panel);
	unsigned int i;
	int ret;

	/* Reset is driven through the STM32 (display_mcu): assert then release. */
	gpiod_set_value_cansleep(ctx->reset, 0);
	msleep(ctx->desc->reset_low_ms);
	gpiod_set_value_cansleep(ctx->reset, 1);
	msleep(ctx->desc->reset_high_ms);

	for (i = 0; i < ctx->desc->init_length; i++) {
		const struct osoyoo_init_cmd *cmd = &ctx->desc->init[i];

		ret = mipi_dsi_dcs_write_buffer(ctx->dsi, cmd->data, cmd->len);
		if (ret < 0)
			return ret;

		if (cmd->delay)
			msleep(cmd->delay);
	}

	return 0;
}

static int osoyoo_panel_unprepare(struct drm_panel *panel)
{
	struct osoyoo_panel *ctx = to_osoyoo_panel(panel);

	mipi_dsi_dcs_set_display_off(ctx->dsi);
	mipi_dsi_dcs_enter_sleep_mode(ctx->dsi);

	if (ctx->reset)
		gpiod_set_value_cansleep(ctx->reset, 0);

	return 0;
}

static int osoyoo_panel_enable(struct drm_panel *panel)
{
	struct osoyoo_panel *ctx = to_osoyoo_panel(panel);
	int ret;

	/*
	 * ST7701S must receive display-on only AFTER vc4 has started the DSI
	 * video stream (enable() runs after the encoder is enabled); on rpi
	 * Trixie/6.12 issuing it during prepare() leaves the panel blanked.
	 * ILI9881C panels issue display-on at the tail of their init table and
	 * do not set this flag.
	 */
	if (ctx->desc->flags & OSOYOO_DISPLAY_ON_IN_ENABLE) {
		ret = mipi_dsi_dcs_set_display_on(ctx->dsi);
		if (ret < 0)
			return ret;
		msleep(50);
	}

	return 0;
}

static int osoyoo_panel_disable(struct drm_panel *panel)
{
	return 0;
}

static int osoyoo_panel_get_modes(struct drm_panel *panel,
				  struct drm_connector *connector)
{
	struct osoyoo_panel *ctx = to_osoyoo_panel(panel);
	const struct drm_display_mode *src = ctx->desc->mode;
	struct drm_display_mode *mode;

	/*
	 * On bcm2712 (Pi 5 / CM5) the DSI goes through RP1, whose D-PHY will not
	 * lock very low link rates. Panels that provide an RP1-specific mode
	 * (higher pixel clock) use it there; every other board keeps the
	 * vc4-proven default mode.
	 */
	if (ctx->desc->mode_rp1 && of_machine_is_compatible("brcm,bcm2712"))
		src = ctx->desc->mode_rp1;

	mode = drm_mode_duplicate(connector->dev, src);
	if (!mode) {
		dev_err(&ctx->dsi->dev, "failed to add mode %ux%u@%u\n",
			src->hdisplay, src->vdisplay,
			drm_mode_vrefresh(src));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;

#ifdef CONFIG_DRM_PANEL_ORIENTATION_QUIRKS
	drm_connector_set_panel_orientation(connector, ctx->orientation);
#endif

	return 1;
}

static enum drm_panel_orientation
osoyoo_panel_get_orientation(struct drm_panel *panel)
{
	struct osoyoo_panel *ctx = to_osoyoo_panel(panel);

	return ctx->orientation;
}

static const struct drm_panel_funcs osoyoo_panel_funcs = {
	.prepare = osoyoo_panel_prepare,
	.unprepare = osoyoo_panel_unprepare,
	.enable = osoyoo_panel_enable,
	.disable = osoyoo_panel_disable,
	.get_modes = osoyoo_panel_get_modes,
	.get_orientation = osoyoo_panel_get_orientation,
};

/* ============================ probe / remove ============================= */

static int osoyoo_panel_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct osoyoo_panel *ctx;
	int ret;

	ctx = devm_kzalloc(&dsi->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dsi = dsi;
	ctx->desc = of_device_get_match_data(&dsi->dev);
	if (!ctx->desc)
		return -ENODEV;

	dev_info(&dsi->dev, "osoyoo dsi panel: %s\n",
		 (char *)of_get_property(dsi->dev.of_node, "compatible", NULL));

	/* DSI host must be powered before we drive the panel. */
	ctx->panel.prepare_prev_first = true;
	drm_panel_init(&ctx->panel, &dsi->dev, &osoyoo_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ctx->reset = devm_gpiod_get_optional(&dsi->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset))
		return dev_err_probe(&dsi->dev, PTR_ERR(ctx->reset),
				     "Couldn't get our reset GPIO\n");

	ret = of_drm_get_panel_orientation(dsi->dev.of_node, &ctx->orientation);
	if (ret) {
		dev_err(&dsi->dev, "%pOF: failed to get orientation: %d\n",
			dsi->dev.of_node, ret);
		return ret;
	}

	/* Owns panel->backlight before attach so display_name gets set. */
	ret = osoyoo_backlight_register(ctx);
	if (ret)
		return ret;	/* propagates EPROBE_DEFER for normal retry */

	drm_panel_add(&ctx->panel);

	dsi->mode_flags = ctx->desc->mode_flags;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->lanes = ctx->desc->lanes;
	dev_info(&dsi->dev, "lanes: %d\n", dsi->lanes);

	ret = mipi_dsi_attach(dsi);
	if (ret)
		drm_panel_remove(&ctx->panel);

	return ret;
}

static void osoyoo_panel_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct osoyoo_panel *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
	if (ctx->reset)
		gpiod_set_value_cansleep(ctx->reset, 0);
	if (ctx->mcu)
		put_device(&ctx->mcu->dev);
}

/* ============================ descriptors =============================== */

static const struct osoyoo_desc osoyoo_st7701s_3p5_desc = {
	.init = osoyoo_st7701s_3p5_init,
	.init_length = ARRAY_SIZE(osoyoo_st7701s_3p5_init),
	.mode = &osoyoo_st7701s_3p5_mode,
	.mode_rp1 = &osoyoo_st7701s_3p5_mode_rp1,
	/*
	 * vc4 drives ST7701 over DSI in burst mode with a non-continuous clock
	 * (matches mainline panel-sitronix-st7701). Match the host, not the
	 * bridge reference.
	 */
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
		      MIPI_DSI_MODE_LPM | MIPI_DSI_CLOCK_NON_CONTINUOUS,
	.lanes = 2,
	.reset_low_ms = 60,
	.reset_high_ms = 120,
	.flags = OSOYOO_DISPLAY_ON_IN_ENABLE,
};

static const struct osoyoo_desc osoyoo_dsi_7inch_desc = {
	.init = osoyoo_dsi_7inch_init,
	.init_length = ARRAY_SIZE(osoyoo_dsi_7inch_init),
	.mode = &osoyoo_dsi_7inch_mode,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_LPM,
	.lanes = 2,
	.reset_low_ms = 60,
	.reset_high_ms = 60,
	.flags = 0,
};

static const struct osoyoo_desc osoyoo_dsi_10inch_2lane_desc = {
	.init = osoyoo_dsi_10inch_2lane_init,
	.init_length = ARRAY_SIZE(osoyoo_dsi_10inch_2lane_init),
	.mode = &osoyoo_dsi_10inch_2lane_mode,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_LPM,
	.lanes = 2,
	.reset_low_ms = 60,
	.reset_high_ms = 60,
	.flags = 0,
};

static const struct osoyoo_desc osoyoo_dsi_10inch_4lane_desc = {
	.init = osoyoo_dsi_10inch_4lane_init,
	.init_length = ARRAY_SIZE(osoyoo_dsi_10inch_4lane_init),
	.mode = &osoyoo_dsi_10inch_4lane_mode,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_LPM,
	.lanes = 4,
	.reset_low_ms = 60,
	.reset_high_ms = 60,
	.flags = 0,
};

static const struct of_device_id osoyoo_panel_of_match[] = {
	{ .compatible = "osoyoo,st7701s-3p5inch",    .data = &osoyoo_st7701s_3p5_desc },
	{ .compatible = "osoyoo,dsi-7inch",          .data = &osoyoo_dsi_7inch_desc },
	{ .compatible = "osoyoo,dsi-10.1inch-2lane", .data = &osoyoo_dsi_10inch_2lane_desc },
	{ .compatible = "osoyoo,dsi-10.1inch-4lane", .data = &osoyoo_dsi_10inch_4lane_desc },
	{ }
};
MODULE_DEVICE_TABLE(of, osoyoo_panel_of_match);

static struct mipi_dsi_driver osoyoo_panel_dsi_driver = {
	.probe = osoyoo_panel_dsi_probe,
	.remove = osoyoo_panel_dsi_remove,
	.driver = {
		.name = "osoyoo-dsi-panel",
		.of_match_table = osoyoo_panel_of_match,
	},
};
module_mipi_dsi_driver(osoyoo_panel_dsi_driver);

MODULE_AUTHOR("OSOYOO");
MODULE_DESCRIPTION("OSOYOO unified MIPI-DSI direct-connect panel driver (ST7701S + ILI9881C)");
MODULE_LICENSE("GPL v2");
