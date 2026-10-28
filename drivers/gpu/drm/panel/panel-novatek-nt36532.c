// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2024 Luka Panio <lukapanio@gmail.com>

#include <linux/backlight.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>
#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>

struct nt36532 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi[2];
	const struct panel_desc *desc;
	struct drm_dsc_config dsc;
	struct regulator_bulk_data supplies[3];
	struct gpio_desc *reset_gpio;
};

static inline struct nt36532 *
to_nt36532(struct drm_panel *panel)
{
	return container_of(panel, struct nt36532, panel);
}

struct panel_desc {
	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;
	struct drm_display_mode drm_mode;

	const struct mipi_dsi_device_info dsi_info;
	int (*init_sequence)(struct nt36532 *ctx);
	void (*reset_sequence)(struct nt36532 *ctx);
	struct drm_dsc_config dsc;
};

/*
 * DIAG (temporary, black-panel hunt): the bootloader leaves this panel
 * displaying its splash, and the panel goes black only once this driver takes
 * over -- the same way on 6.17 and 7.0, so it is the shared handover that
 * breaks it, not either kernel.  The handover does two things: reset the
 * panel and replay the 216-packet init sequence.  Set this to skip both and
 * leave whatever the bootloader programmed in place, which tells the two
 * apart on the same image:
 *
 *	panel_novatek_nt36532.skip_panel_init=1   (built-in, so via cmdline)
 */
static bool skip_panel_init;
module_param(skip_panel_init, bool, 0644);
MODULE_PARM_DESC(skip_panel_init, "skip panel reset and init sequence (diagnostic)");

static struct mipi_dsi_device *nt36532_cmd_dsi(struct nt36532 *ctx)
{
	return ctx->dsi[1] ?: ctx->dsi[0];
}

static void pipa_reset(struct nt36532 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(12000, 13000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(12000, 13000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(12000, 13000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(12000, 13000);
}

/* Vendor reset sequence for the Tianma module: <0 10 1 3 0 3 1 15>. */
static void pipa_tm_reset(struct nt36532 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(3000, 4000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(3000, 4000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(15000, 16000);
}

static void yudi_reset(struct nt36532 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(3000, 4000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(15000, 16000);
}

static int pipa_init_sequence(struct nt36532 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = nt36532_cmd_dsi(ctx) };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x27);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd0, 0x31);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd1, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd2, 0x38);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xde, 0x43);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdf, 0x02);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x23);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_NOP, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SOFT_RESET, 0x84);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x05, 0x2d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x06, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x11, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x12, 0x2a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x15, 0xd0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x16, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_DISPLAY_ON, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_PARTIAL_ROWS, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_PARTIAL_COLUMNS, 0xfe);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x32, 0xfd);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_SCROLL_AREA, 0xfb);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_TEAR_OFF, 0xf8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_TEAR_ON, 0xf5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_ADDRESS_MODE, 0xf3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_SCROLL_START, 0xf2);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_EXIT_IDLE_MODE, 0xf2);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_ENTER_IDLE_MODE, 0xf2);
	mipi_dsi_dcs_set_pixel_format_multi(&dsi_ctx, 0xef);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3b, 0xec);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_3D_CONTROL, 0xe9);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_GET_3D_CONTROL, 0xe5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_VSYNC_TIMING, 0xe5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x41, 0xe5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_COLUMN_ADDRESS, 0x13);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_GET_SCANLINE, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x46, 0xf4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x47, 0xe7);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x48, 0xda);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x49, 0xcd);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4a, 0xc0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4b, 0xb3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4c, 0xb1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4d, 0xb1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4e, 0xb1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4f, 0x95);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x50, 0x79);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x5c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_GET_DISPLAY_BRIGHTNESS, 0x58);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x58);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_GET_CONTROL_DISPLAY, 0x58);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_PAGE_ADDRESS, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x58, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x59, 0xfb);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5a, 0xf7);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5b, 0xf3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5c, 0xef);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5d, 0xe3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5e, 0xd8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5f, 0xd6);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x60, 0xd6);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x61, 0xd6);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x62, 0xc8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x63, 0xb7);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x64, 0xaa);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x65, 0xa8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x66, 0xa8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x67, 0xa8);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x65, 0xcc);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6d, 0xcc);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x32, 0x72);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x17, 0x11);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x22);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9f, 0x57);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x1f, 0x1f, 0x1f, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb1, 0x4d, 0x4d, 0x4d, 0x4d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb2, 0x1e, 0x1e, 0x1e, 0x1e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb3, 0x6c, 0x6c, 0x6c, 0x6c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb4, 0x1f, 0x1f, 0x1f, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb5, 0x44, 0x44, 0x44, 0x44);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb8, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb9, 0x6e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xba, 0x6e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbb, 0x6e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbe, 0x0b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbf, 0x6e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf8, 0xcc);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf9, 0xcc);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc1, 0x6e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc3, 0x6e);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x23);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xba, 0x7a, 0x5d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbb, 0x77, 0x60);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1c, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x92, 0x45, 0x00, 0xc0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdb, 0x33);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x25);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_GET_ERROR_COUNT_ON_DSI, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x23, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x24, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_COLUMN_ADDRESS, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_PAGE_ADDRESS, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x42, 0x0b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc5, 0x1e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf6, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf7, 0x48);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x26);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x04, 0x75);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x19, 0x10, 0x12, 0x12, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1a, 0xc5, 0xa3, 0xa3, 0xa3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1b, 0x0f, 0x11, 0x11, 0x11);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1c, 0xe8, 0xc6, 0xc6, 0xc6);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1e, 0x45);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1f, 0x45);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2a, 0x10, 0x12, 0x12, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2b, 0xc0, 0x9e, 0x9e, 0x9e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2f, 0x0b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x30, 0x45);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x32, 0x45);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x33, 0x22);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x34, 0x92);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x35, 0x78);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x36, 0x96);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x37, 0x78);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x38, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3a, 0x45);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_VSYNC_TIMING, 0x47);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x41, 0x47);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x42, 0x47);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x45, 0x0b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x46, 0x47);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x48, 0x47);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4a, 0x47);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x80, 0xcc);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x81, 0xcc);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x84, 0x16, 0x16, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x85, 0x26, 0x26, 0x26);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8b, 0xaa);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x99, 0x26, 0x26, 0x26, 0x26);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9a, 0xb6, 0xb6, 0xb6, 0xb6);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9b, 0x25, 0x25, 0x25, 0x25);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9c, 0xd4, 0xd4, 0xd4, 0xd4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9d, 0x26, 0x26, 0x26, 0x26);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9e, 0xac, 0xac, 0xac, 0xac);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x27);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x01, 0xc1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x07, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x08, 0x3c, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0a, 0xeb, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0c, 0x4b, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0e, 0xeb, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x10, 0x4b, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x14, 0x33);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x76, 0xf0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x77, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x79, 0x29);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7e, 0x4e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x80, 0xc9, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x81, 0x3d, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x82, 0xcb, 0xc0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x84, 0x92, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x85, 0x36, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x86, 0xcb, 0xc0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x88, 0x92, 0x00);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x2a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x14, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1e, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1f, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa3, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc5, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc6, 0x16);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb3, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb2, 0x91);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x90, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x91,
			       0x89, 0x28, 0x00, 0x14, 0xd2, 0x00, 0x01, 0xf4,
			       0x01, 0xab, 0x00, 0x06, 0x05, 0x7a, 0x06, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x92, 0x10, 0xf0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3b, 0x03, 0xd8, 0x1a, 0x0a, 0x0a, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x51, 0x0f, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x53, 0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x35, 0x00);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 40);

	return dsi_ctx.accum_err;
}

static int m81_36_02_0a_init_sequence(struct nt36532 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = nt36532_cmd_dsi(ctx) };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x27);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd0, 0x31);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd1, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd2, 0x38);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xde, 0x43);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdf, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x23);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_NOP, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SOFT_RESET, 0x84);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x05, 0x2d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x06, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x11, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x12, 0x2a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x15, 0xd0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x16, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x29, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x30, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x31, 0xfe);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x32, 0xfd);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x33, 0xfb);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x34, 0xf8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x35, 0xf5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x36, 0xf3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x37, 0xf2);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x38, 0xf2);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x39, 0xf2);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3a, 0xef);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3b, 0xec);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3d, 0xe9);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3f, 0xe5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x40, 0xe5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x41, 0xe5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2a, 0x13);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x45, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x46, 0xf4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x47, 0xe7);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x48, 0xda);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x49, 0xcd);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4a, 0xc0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4b, 0xb3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4c, 0xb1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4d, 0xb1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4e, 0xb1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4f, 0x95);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x50, 0x79);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x51, 0x5c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x52, 0x58);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x53, 0x58);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x54, 0x58);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2b, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x58, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x59, 0xfb);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5a, 0xf7);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5b, 0xf3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5c, 0xef);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5d, 0xe3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5e, 0xd8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5f, 0xd6);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x60, 0xd6);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x61, 0xd6);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x62, 0xc8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x63, 0xb7);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x64, 0xaa);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x65, 0xa8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x66, 0xa8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x67, 0xa8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x65, 0xcc);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6d, 0xcc);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x31, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x32, 0x72);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x05, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x07, 0x37);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x08, 0x19);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0f, 0x6e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x17, 0x11);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9d, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9e, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x22);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9f, 0x57);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x1d, 0x1d, 0x1d, 0x1d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb1, 0xce, 0xce, 0xce, 0xce);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb2, 0x1c, 0x1c, 0x1c, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb3, 0xee, 0xee, 0xee, 0xee);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb4, 0x1d, 0x1d, 0x1d, 0x1d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb5, 0xc6, 0xc6, 0xc6, 0xc6);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb8, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf8, 0xcc);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf9, 0xcc);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x23);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xba, 0x68, 0x5d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbb, 0x65, 0x60);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1c, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x92, 0x47, 0x00, 0xc0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x94, 0xd8, 0x00, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x95, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x98, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc3, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc7, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xda, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x25);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x05, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x14, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x16, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x20, 0x39);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x24, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x26, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x27, 0x3d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2b, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x42, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbd, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc5, 0x1e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdc, 0xb6);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xde, 0x6d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf7, 0x48);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x26);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x04, 0x75);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x13, 0xcc);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x14, 0xdf);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x19, 0x11, 0x12, 0x12, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1a, 0x0f, 0xc7, 0xc7, 0xc7);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1b, 0x10, 0x11, 0x11, 0x11);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1c, 0x32, 0xea, 0xea, 0xea);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2a, 0x11, 0x12, 0x12, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2b, 0x0a, 0xc2, 0xc2, 0xc2);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x33, 0x91);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x34, 0x78);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x35, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x80, 0xcc);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x81, 0xcc);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8b, 0x8c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x97, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x99, 0x1d, 0x1d, 0x1d, 0x1d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9a, 0x66, 0x66, 0x66, 0x66);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9b, 0x1c, 0x1c, 0x1c, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9c, 0x86, 0x86, 0x86, 0x86);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9d, 0x1d, 0x1d, 0x1d, 0x1d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9e, 0x5e, 0x5e, 0x5e, 0x5e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcd, 0x5d, 0x68);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xce, 0x5a, 0x6a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x27);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_NOP, 0x81);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SOFT_RESET, 0xba);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x03, 0xd6);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x05, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x07, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x08, 0xf4, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x09, 0x29, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0a, 0xe4, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0b, 0x0c, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0c, 0x46, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0d, 0x24, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0e, 0xe4, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0f, 0x0c, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x10, 0x46, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x13, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x14, 0x33);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5b, 0xc7);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x76, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x78, 0x83);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x79, 0x23);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7b, 0xd6);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7d, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7e, 0x4d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7f, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x80, 0x40, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x81, 0x3c, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x82, 0xc4, 0xc0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x83, 0x0c, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x84, 0x6b, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x85, 0x35, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x86, 0xc4, 0xc0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x87, 0x0c, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x88, 0x6b, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x2a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x33, 0x70);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x64, 0x26);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6a, 0x26);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x79, 0xae);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7c, 0x26);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7f, 0x26);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x82, 0x26);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x85, 0x2e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x91, 0x2e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0xd0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x09, 0x77);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0xe0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x14, 0x60);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x16, 0xc0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb3, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb2, 0x91);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x90, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x91, 0x89, 0x28, 0x00, 0x14, 0xd2, 0x00, 0x01, 0xf4,
			       0x01, 0xab, 0x00, 0x06, 0x05, 0x7a, 0x06, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x92, 0x10, 0xf0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9d, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3b, 0x03, 0xd8, 0x1a, 0x0a, 0x0a, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x51, 0x0f, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x53, 0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x35, 0x00);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 40);

	return dsi_ctx.accum_err;
}
static int yudi_init_sequence(struct nt36532 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = nt36532_cmd_dsi(ctx) };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x27);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd0, 0x31);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd1, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd2, 0x38);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xde, 0x43);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdf, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x23);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x01, 0x84);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x05, 0x2d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x06, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x11, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x12, 0x2a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x15, 0xd0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x16, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x29, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_PARTIAL_ROWS, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_PARTIAL_COLUMNS,
				     0xfe);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x32, 0xfd);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x33, 0xfb);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x34, 0xf8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x35, 0xf5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_ADDRESS_MODE, 0xf3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x37, 0xf2);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x38, 0xf2);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x39, 0xf2);
	mipi_dsi_dcs_set_pixel_format_multi(&dsi_ctx, 0xef);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3b, 0xec);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_3D_CONTROL, 0xe9);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3f, 0xe5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_VSYNC_TIMING, 0xe5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x41, 0xe5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2a, 0x13);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_GET_SCANLINE, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x46, 0xf4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x47, 0xe7);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x48, 0xda);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x49, 0xcd);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4a, 0xc0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4b, 0xb3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4c, 0xb1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4d, 0xb1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4e, 0xb1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4f, 0x95);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x50, 0x79);
	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0x005c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x52, 0x58);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x58);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x54, 0x58);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2b, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x58, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x59, 0xfb);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5a, 0xf7);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5b, 0xf3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5c, 0xef);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5d, 0xe3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_CABC_MIN_BRIGHTNESS,
				     0xd8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5f, 0xd6);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x60, 0xd6);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x61, 0xd6);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x62, 0xc8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x63, 0xb7);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x64, 0xaa);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x65, 0xa8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x66, 0xa8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x67, 0xa8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x2a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbc, 0x77, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x26);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x04, 0x6e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8b, 0x37);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x22);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9f, 0x52);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x27);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0e, 0x35, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x10);
	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0xff0f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x24);
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x27);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x13, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x14, 0x33);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf2, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf3, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf4, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf5, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf6, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf9, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x17, 0x11);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0xe0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x46, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x32, 0x52);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb2, 0x91);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb3, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3b,
				     0x03, 0x78, 0x1a, 0x0a, 0x0a, 0x00);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 100);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
}

/*
 * DIAG (temporary, black-panel hunt): the display comes up with every register
 * matching the vendor and the bootloader's splash proving the panel, its rails,
 * the DSI link and the DSC path all work, yet nothing reaches the glass.  The
 * one thing that cannot be compared register for register is the DSC bitstream
 * itself, so this mode bypasses it: 60 Hz uncompressed RGB888 fits in the link
 * (900 px x 24 bpp x 60 Hz per link is about 1.2 Gbps per lane) and the DDIC
 * decodes plain RGB until it is sent a PPS, so no PPS means no DSC.
 *
 *	no_dsc=1 on the kernel command line
 */
static bool no_dsc;
module_param(no_dsc, bool, 0644);
MODULE_PARM_DESC(no_dsc, "run the panel uncompressed at 60 Hz (diagnostic)");

/*
 * DIAG (temporary): the vendor runs this panel from 30 to 144 fps at a fixed
 * 1,007,032,320 Hz link clock, stretching the porches; 60 fps is the 120 fps
 * porch set with VFP doubled.  0 keeps the descriptor's own mode.
 */
static unsigned int force_fps;
module_param(force_fps, uint, 0644);
MODULE_PARM_DESC(force_fps, "force a panel frame rate (60/120/144); 0 = descriptor default");

/*
 * DIAG (temporary): the vendor parses DSC per interface (h_active = 900), so the
 * PPS it sends carries a per-link picture width.  This driver hands the panel
 * the full bonded width (1800), from which the decoder derives four 450-px
 * slices per line even though each link only carries two.  Boot with
 * panel_novatek_nt36532.pps_per_link_pic_width=1 to send the per-link width,
 * leaving the DPU's split-panel encoder configuration untouched.
 */
static bool pps_per_link_pic_width;
module_param_named(pps_per_link_pic_width, pps_per_link_pic_width, bool, 0644);
MODULE_PARM_DESC(pps_per_link_pic_width,
	"pack the PPS with the per-link picture width instead of the bonded width");

static const struct drm_display_mode pipa_mode_60 = {
	.clock = (1800 + 200 + 4 + 92) * (2880 + 26 + 2 + 214) * 60 / 1000,
	.hdisplay = 1800,
	.hsync_start = 1800 + 200,
	.hsync_end = 1800 + 200 + 4,
	.htotal = 1800 + 200 + 4 + 92,
	.vdisplay = 2880,
	.vsync_start = 2880 + 26,
	.vsync_end = 2880 + 26 + 2,
	.vtotal = 2880 + 26 + 2 + 214,
	.width_mm = 148,
	.height_mm = 237,
	.type = DRM_MODE_TYPE_DRIVER,
};

static const struct drm_display_mode pipa_mode_120 = {
	.clock = (1800 + 200 + 4 + 92) * (2880 + 26 + 2 + 214) * 120 / 1000,
	.hdisplay = 1800,
	.hsync_start = 1800 + 200,
	.hsync_end = 1800 + 200 + 4,
	.htotal = 1800 + 200 + 4 + 92,
	.vdisplay = 2880,
	.vsync_start = 2880 + 26,
	.vsync_end = 2880 + 26 + 2,
	.vtotal = 2880 + 26 + 2 + 214,
	.width_mm = 148,
	.height_mm = 237,
	.type = DRM_MODE_TYPE_DRIVER,
};


/*
 * Vendor 60 Hz timing, read back from the stock kernel while it was scanning
 * this panel at 60 fps:
 *   dump_info: 900(46|100|2)x2880(214|3148|2)@60fps 1007032320 Hz
 *   dsi0 TOTAL = 0x186301bf, clocks unchanged (pclk 167838720, byteclk 125879040)
 * So it is the 120 Hz porch set with VFP doubled: htotal stays 2096, vtotal
 * goes 3122 -> 6244, and the derived mode clock is the same 785245 kHz.
 */
static const struct drm_display_mode pipa_mode_60_dsc = {
	.clock = (1800 + 200 + 4 + 92) * (2880 + 3148 + 2 + 214) * 60 / 1000,
	.hdisplay = 1800,
	.hsync_start = 1800 + 200,
	.hsync_end = 1800 + 200 + 4,
	.htotal = 1800 + 200 + 4 + 92,
	.vdisplay = 2880,
	.vsync_start = 2880 + 3148,
	.vsync_end = 2880 + 3148 + 2,
	.vtotal = 2880 + 3148 + 2 + 214,
	.width_mm = 148,
	.height_mm = 237,
	.type = DRM_MODE_TYPE_DRIVER,
};

/*
 * Tianma module timing from the vendor: the stock kernel's DRM state runs
 * 1800x2880 at 50 Hz with htotal 2394 / vtotal 5620, and Android runs the
 * same porches at 144 Hz.  pipa_mode_120 is the DT default, not the mode
 * this unit uses.
 */
static const struct drm_display_mode pipa_mode_144 = {
	.clock = 1937416,
	.hdisplay = 1800,
	.hsync_start = 2298,
	.hsync_end = 2302,
	.htotal = 2394,
	.vdisplay = 2880,
	.vsync_start = 5404,
	.vsync_end = 5406,
	.vtotal = 5620,
	.width_mm = 148,
	.height_mm = 237,
	.type = DRM_MODE_TYPE_DRIVER,
};

static const struct drm_display_mode yudi_mode_120 = {
	.clock = 2 * (1440 + 65 + 10 + 38) * (1800 + 26 + 2 + 118) * 120 / 1000,
	.hdisplay = 2 * (1440),
	.hsync_start = 2 * (1440 + 65),
	.hsync_end = 2 * (1440 + 65 + 10),
	.htotal = 2 * (1440 + 65 + 10 + 38),
	.vdisplay = 1800,
	.vsync_start = 1800 + 26,
	.vsync_end = 1800 + 26 + 2,
	.vtotal = 1800 + 26 + 2 + 118,
	.width_mm = 301,
	.height_mm = 188,
	.type = DRM_MODE_TYPE_DRIVER,
};

static const struct panel_desc pipa_desc = {
	.dsi_info = {
		.type = "pipa",
		.channel = 0,
		.node = NULL,
	},
	.dsc = {
		.dsc_version_major = 1,
		.dsc_version_minor = 1,
		.slice_height = 20,
		.slice_width = 450,
		/* Downstream sets this while parsing DT */
		.slice_count = 2,

		.bits_per_component = 8,
		.bits_per_pixel = 8 << 4, /* 4 fractional bits */
		.block_pred_enable = true,
	},
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_CLOCK_NON_CONTINUOUS,
	.drm_mode = pipa_mode_120,
	.init_sequence = pipa_init_sequence,
	.reset_sequence = pipa_reset
};

/*
 * The Tianma module uses the vendor 144 Hz timing, its own init sequence
 * and the vendor reset sequence.  The DSC parameters are shared by the
 * vendor's Tianma and CSOT nodes; the link flags carry VIDEO_BURST to
 * match the vendor's burst_mode.
 */
static const struct panel_desc pipa_tm_desc = {
	.dsi_info = {
		.type = "pipa",
		.channel = 0,
		.node = NULL,
	},
	.dsc = {
		.dsc_version_major = 1,
		.dsc_version_minor = 1,
		.slice_height = 20,
		.slice_width = 450,
		/* Downstream sets this while parsing DT */
		.slice_count = 2,

		.bits_per_component = 8,
		.bits_per_pixel = 8 << 4, /* 4 fractional bits */
		.block_pred_enable = true,
	},
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
		MIPI_DSI_CLOCK_NON_CONTINUOUS,
	.drm_mode = pipa_mode_60_dsc,
	.init_sequence = m81_36_02_0a_init_sequence,
	.reset_sequence = pipa_tm_reset
};

static const struct panel_desc yudi_desc = {
	.dsi_info = {
		.type = "yudi",
		.channel = 0,
		.node = NULL,
	},
	.dsc = {
		.dsc_version_major = 1,
		.dsc_version_minor = 1,
		.slice_height = 20,
		.slice_width = 720,
		/* Downstream sets this while parsing DT */
		.slice_count = 2,

		.bits_per_component = 8,
		.bits_per_pixel = 8 << 4, /* 4 fractional bits */
		.block_pred_enable = true,
	},
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST | MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM,
	.drm_mode = yudi_mode_120,
	.init_sequence = yudi_init_sequence,
	.reset_sequence = yudi_reset
};

static int nt36532_off(struct nt36532 *ctx)
{
	struct mipi_dsi_device *dsi = nt36532_cmd_dsi(ctx);
	struct device *dev = &dsi->dev;
	int ret;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display off: %d\n", ret);
		return ret;
	}
	msleep(20);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode: %d\n", ret);
		return ret;
	}
	msleep(120);


	return 0;
}

/*
 * DIAG (temporary, black-panel hunt): the DSI/MDP register state is readable
 * from userspace over the rescue shell, but the panel's own power/display
 * state is not.  Read it back so "panel never woke up" and "panel is awake but
 * shows nothing" stop being guesses.  0x0A bit 2 = display on, bit 3 = normal
 * mode, bit 4 = sleep out.  Remove this helper once the panel lights.
 */
static void nt36532_diag_panel_state(struct nt36532 *ctx, const char *when)
{
	struct mipi_dsi_device *dsi = nt36532_cmd_dsi(ctx);
	ssize_t ret;
	u8 power_mode = 0xff;
	u8 display_mode = 0xff;

	ret = mipi_dsi_dcs_read(dsi, MIPI_DCS_GET_POWER_MODE, &power_mode, 1);
	dev_info(&dsi->dev, "diag[%s]: DCS 0x0A ret=%zd power_mode=0x%02x\n",
		 when, ret, power_mode);

	ret = mipi_dsi_dcs_read(dsi, MIPI_DCS_GET_DISPLAY_MODE, &display_mode, 1);
	dev_info(&dsi->dev, "diag[%s]: DCS 0x0D ret=%zd display_mode=0x%02x\n",
		 when, ret, display_mode);
}

/*
 * DIAG (temporary, black-panel hunt): the panel answers DCS reads and reports
 * 0x9e while the stock ROM reports 0x9d (DCS 0x0a: bit 0 booster off, bit 1
 * idle mode on).  Rather than guess which command flips those bits, expose a
 * node that sends an arbitrary DCS command and immediately reports the power
 * mode again, so the answer can be found on the running unit.
 *
 *	echo "0x38" > /sys/kernel/debug/nt36532/dcs	# exit idle mode
 *
 * Remove together with nt36532_diag_panel_state() once the panel lights.
 */
static ssize_t nt36532_diag_dcs_write(struct file *file,
				      const char __user *ubuf,
				      size_t len, loff_t *ppos)
{
	struct nt36532 *ctx = file->private_data;
	struct mipi_dsi_device *dsi = nt36532_cmd_dsi(ctx);
	u8 cmd[64];
	size_t n = 0;
	char *str, *cur, *tok;
	ssize_t ret;
	u8 power_mode = 0xff;

	if (len == 0 || len > 200)
		return -EINVAL;

	str = memdup_user_nul(ubuf, len);
	if (IS_ERR(str))
		return PTR_ERR(str);

	cur = str;
	while ((tok = strsep(&cur, " \t\n")) != NULL) {
		u8 byte;

		if (!*tok)
			continue;
		if (kstrtou8(tok, 0, &byte) || n >= ARRAY_SIZE(cmd)) {
			kfree(str);
			return -EINVAL;
		}
		cmd[n++] = byte;
	}
	kfree(str);

	if (n < 1)
		return -EINVAL;

	ret = mipi_dsi_dcs_write(dsi, cmd[0], cmd + 1, n - 1);
	msleep(60);

	mipi_dsi_dcs_read(dsi, MIPI_DCS_GET_POWER_MODE, &power_mode, 1);
	dev_info(&dsi->dev,
		 "poke: cmd=0x%02x len=%zu write_ret=%zd power_mode=0x%02x\n",
		 cmd[0], n - 1, ret, power_mode);

	return len;
}

static ssize_t nt36532_diag_dcs_read(struct file *file, char __user *ubuf,
				     size_t len, loff_t *ppos)
{
	struct nt36532 *ctx = file->private_data;
	struct mipi_dsi_device *dsi = nt36532_cmd_dsi(ctx);
	char buf[256];
	u8 power_mode = 0xff, display_mode = 0xff;
	u8 addr_mode = 0xff, pixel_format = 0xff;
	u8 line = 0, first = 0, last = 0;
	bool advancing = false;
	int i, n;

	mipi_dsi_dcs_read(dsi, MIPI_DCS_GET_POWER_MODE, &power_mode, 1);
	mipi_dsi_dcs_read(dsi, MIPI_DCS_GET_DISPLAY_MODE, &display_mode, 1);
	mipi_dsi_dcs_read(dsi, MIPI_DCS_GET_ADDRESS_MODE, &addr_mode, 1);
	mipi_dsi_dcs_read(dsi, MIPI_DCS_GET_PIXEL_FORMAT, &pixel_format, 1);

	/*
	 * DIAG (temporary): DCS 0x45 advances with the panel's own scan, so a
	 * changing value proves the DDIC is locked to the stream we send; a
	 * frozen value means it never locked, whatever the DSI registers say.
	 */
	for (i = 0; i < 8; i++) {
		if (mipi_dsi_dcs_read(dsi, MIPI_DCS_GET_SCANLINE, &line, 1) < 0)
			break;
		if (i == 0)
			first = line;
		if (i > 0 && line != last)
			advancing = true;
		last = line;
		msleep(20);
	}

	n = snprintf(buf, sizeof(buf),
		     "power=0x%02x display=0x%02x addr=0x%02x pixfmt=0x%02x\n"
		     "scanline samples=%d first=%u last=%u advancing=%s\n",
		     power_mode, display_mode, addr_mode, pixel_format,
		     i, first, last, advancing ? "yes" : "no");

	return simple_read_from_buffer(ubuf, len, ppos, buf, n);
}

static const struct file_operations nt36532_diag_dcs_fops = {
	.open = simple_open,
	.write = nt36532_diag_dcs_write,
	.read = nt36532_diag_dcs_read,
};

static void nt36532_diag_poke_init(struct nt36532 *ctx)
{
	struct dentry *dir;

	dir = debugfs_create_dir("nt36532", NULL);
	if (IS_ERR_OR_NULL(dir))
		return;

	debugfs_create_file("dcs", 0600, dir, ctx, &nt36532_diag_dcs_fops);
}


static void nt36532_pack_pps(struct nt36532 *ctx,
			     struct drm_dsc_picture_parameter_set *pps)
{
	struct drm_dsc_config dsc = ctx->dsc;

	if (pps_per_link_pic_width && ctx->dsi[1])
		dsc.pic_width /= 2;

	drm_dsc_pps_payload_pack(pps, &dsc);
}

static int nt36532_prepare(struct drm_panel *panel)
{
	struct nt36532 *ctx = to_nt36532(panel);
	struct mipi_dsi_device *dsi = nt36532_cmd_dsi(ctx);
	struct device *dev = &dsi->dev;
	struct drm_dsc_picture_parameter_set pps;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	msleep(120);

	ctx->dsi[0]->mode_flags |= MIPI_DSI_MODE_LPM;
	if (ctx->dsi[1])
		ctx->dsi[1]->mode_flags |= MIPI_DSI_MODE_LPM;

	if (skip_panel_init) {
		dev_info(dev, "diag: skipping panel reset and init sequence\n");
	} else {
		ctx->desc->reset_sequence(ctx);

		msleep(120);

		ret = ctx->desc->init_sequence(ctx);
		if (ret < 0) {
			dev_err(panel->dev, "failed to initialize panel: %d\n", ret);
			goto fail;
		}
	}

	nt36532_diag_panel_state(ctx, "post-init");

	if (!no_dsc) {
		nt36532_pack_pps(ctx, &pps);

		dev_info(panel->dev,
			 "dsc-pre: pic %ux%u slice %ux%u count %u chunk %u bpp %u bpc %u per_link=%d\n",
			 ctx->dsc.pic_width, ctx->dsc.pic_height,
			 ctx->dsc.slice_width, ctx->dsc.slice_height,
			 ctx->dsc.slice_count, ctx->dsc.slice_chunk_size,
			 ctx->dsc.bits_per_pixel, ctx->dsc.bits_per_component,
			 pps_per_link_pic_width);
		print_hex_dump(KERN_INFO, "PPS-pre:", DUMP_PREFIX_NONE, 16, 1,
			       (void *)&pps, sizeof(pps), false);

		ret = mipi_dsi_picture_parameter_set(dsi, &pps);
		if (ret < 0) {
			dev_err(panel->dev, "failed to transmit PPS: %d\n", ret);
			goto fail;
		}

		ret = mipi_dsi_compression_mode(dsi, true);
		if (ret < 0) {
			dev_err(dev, "failed to enable compression mode: %d\n",
				ret);
			goto fail;
		}
	} else {
		dev_info(dev, "diag: no_dsc set, panel runs uncompressed at 60 Hz\n");
	}

	msleep(120);

	nt36532_diag_panel_state(ctx, "prepared");

	return 0;

fail:
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	return ret;
}

static int nt36532_enable(struct drm_panel *panel)
{
	struct nt36532 *ctx = to_nt36532(panel);
	struct drm_dsc_picture_parameter_set pps;
	struct mipi_dsi_device *dsi = nt36532_cmd_dsi(ctx);
	struct device *dev = &dsi->dev;
	int ret;

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}

	usleep_range(10000, 11000);

	/*
	 * The no_dsc diagnostic leaves the DSI host uncompressed; sending the
	 * PPS here would tell the panel to decode a bitstream that is no longer
	 * there, so both ends must agree on the mode.
	 */
	if (!no_dsc) {
		nt36532_pack_pps(ctx, &pps);

		print_hex_dump(KERN_INFO, "DSC:", DUMP_PREFIX_NONE, 16,
		       1, (void *)&pps, sizeof(pps), false);

		ret = mipi_dsi_picture_parameter_set(dsi, &pps);
		if (ret < 0) {
			dev_err(panel->dev, "failed to transmit PPS: %d\n", ret);
			return ret;
		}

		msleep(28);
	}

	nt36532_diag_panel_state(ctx, "enabled");

	return ret;
}

static int nt36532_disable(struct drm_panel *panel)
{
	struct nt36532 *ctx = to_nt36532(panel);
	struct device *dev = &ctx->dsi[0]->dev;
	int ret;

	ret = nt36532_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	return ret;
}

static int nt36532_unprepare(struct drm_panel *panel)
{
	struct nt36532 *ctx = to_nt36532(panel);

	ctx->dsi[0]->mode_flags &= ~MIPI_DSI_MODE_LPM;
	if (ctx->dsi[1])
		ctx->dsi[1]->mode_flags &= ~MIPI_DSI_MODE_LPM;

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);

	return 0;
}

static int nt36532_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	struct nt36532 *ctx = to_nt36532(panel);
	const struct drm_display_mode *mode;

	switch (force_fps) {
	case 60:
		mode = &pipa_mode_60_dsc;
		break;
	case 120:
		mode = &pipa_mode_120;
		break;
	case 144:
		mode = &pipa_mode_144;
		break;
	default:
		mode = &ctx->desc->drm_mode;
		break;
	}

	if (no_dsc)
		mode = &pipa_mode_60;
	return drm_connector_helper_get_modes_fixed(connector, mode);
}

static const struct drm_panel_funcs nt36532_panel_funcs = {
	.prepare = nt36532_prepare,
	.enable = nt36532_enable,
	.disable = nt36532_disable,
	.unprepare = nt36532_unprepare,
	.get_modes = nt36532_get_modes,
};

static int nt36532_probe(struct mipi_dsi_device *dsi)
{
	struct mipi_dsi_host *dsi_sec_host;
	struct nt36532 *ctx;
	struct device *dev = &dsi->dev;
	struct device_node *dsi_sec;
	int ret, i;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->supplies[0].supply = "vddio";
	ctx->supplies[1].supply = "dvddbuck";
	ctx->supplies[2].supply = "dvddldo";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->desc = of_device_get_match_data(dev);
	if (!ctx->desc)
		return -ENODEV;

	/*
	 * liuqin (Xiaomi Pad 6 Pro) is fitted with one of two NT36532 panel
	 * modules and their init sequences are not interchangeable: the other
	 * module's sequence leaves the panel black.  The bootloader names the
	 * fitted display module on the kernel command line
	 * (msm_drm.dsi_display0=qcom,mdss_dsi_<module>_...), so select the
	 * matching init sequence from there.  Known modules: m81_36_02_0a ->
	 * Tianma, m81_42_02_0b -> CSOT.  An unlisted module keeps the init
	 * sequence of the matched desc.
	 */
	if (ctx->desc == &pipa_desc && saved_command_line &&
	    strstr(saved_command_line, "mdss_dsi_m81_36_02_0a"))
		ctx->desc = &pipa_tm_desc;

	dev_info(dev,
		 "panel desc: %s, htotal=%u vtotal=%u clock=%u kHz, force_fps=%u, cmdline_match=%d\n",
		 ctx->desc == &pipa_tm_desc ? "tianma" : "csot",
		 ctx->desc->drm_mode.htotal, ctx->desc->drm_mode.vtotal,
		 ctx->desc->drm_mode.clock, force_fps,
		 saved_command_line &&
		 strstr(saved_command_line, "mdss_dsi_m81_36_02_0a") ? 1 : 0);

	dsi_sec = of_graph_get_remote_node(dsi->dev.of_node, 1, -1);

	if (dsi_sec) {
		const struct mipi_dsi_device_info info = { "nt36532", 0,
							   dsi_sec };

		dev_notice(dev, "Using Dual-DSI with OF node `%s`\n", dsi_sec->name);

		dsi_sec_host = of_find_mipi_dsi_host_by_node(dsi_sec);
		of_node_put(dsi_sec);
		if (!dsi_sec_host) {
			return dev_err_probe(dev, -EPROBE_DEFER,
					     "Cannot get secondary DSI host\n");
		}

		ctx->dsi[1] =
			devm_mipi_dsi_device_register_full(dev, dsi_sec_host, &info);
		if (IS_ERR(ctx->dsi[1])) {
			return dev_err_probe(dev, PTR_ERR(ctx->dsi[1]),
					     "Cannot get secondary DSI node\n");
		}

		mipi_dsi_set_drvdata(ctx->dsi[1], ctx);
	} else {
		dev_notice(dev, "Using Single-DSI\n");
	}

	ctx->dsi[0] = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	drm_panel_init(&ctx->panel, dev, &nt36532_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	drm_panel_add(&ctx->panel);

	memcpy(&ctx->dsc, &ctx->desc->dsc, sizeof(ctx->desc->dsc));

	for (i = 0; i < ARRAY_SIZE(ctx->dsi); i++) {
		if (!ctx->dsi[i])
			continue;
		/* This panel normally runs DSC; no_dsc bypasses it for diagnosis */
		if (!no_dsc) {
			ctx->dsi[i]->dsc = &ctx->dsc;
			/* vendor packs two 450-px slices into each video packet */
			ctx->dsi[i]->dsc_slice_per_pkt = 2;
		}

		ctx->dsi[i]->lanes = 4;
		ctx->dsi[i]->format = MIPI_DSI_FMT_RGB888;
		ctx->dsi[i]->mode_flags = ctx->desc->mode_flags;

		ret = devm_mipi_dsi_attach(dev, ctx->dsi[i]);

		if (ret < 0) {
			drm_panel_remove(&ctx->panel);
			return dev_err_probe(dev, ret,
					     "Failed to attach to DSI%d\n", i);
		}
	}

	nt36532_diag_poke_init(ctx);

	return 0;
}

static void nt36532_remove(struct mipi_dsi_device *dsi)
{
	struct nt36532 *ctx = mipi_dsi_get_drvdata(dsi);
	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id nt36532_of_match[] = {
	{
		.compatible = "xiaomi,pipa-nt36532",
		.data = &pipa_desc,
	},
	{
		.compatible = "xiaomi,yudi-nt36532",
		.data = &yudi_desc,
	},
	{},
};
MODULE_DEVICE_TABLE(of, nt36532_of_match);

static struct mipi_dsi_driver nt36532_driver = {
	.probe = nt36532_probe,
	.remove = nt36532_remove,
	.driver = {
		.name = "panel-nt36532",
		.of_match_table = nt36532_of_match,
	},
};
module_mipi_dsi_driver(nt36532_driver);

MODULE_AUTHOR("Luka Panio <luakapnio@gmail.com>");
MODULE_AUTHOR("Sophie 'Tyalie' Friedrich <kernel@flowerpot.me>");
MODULE_AUTHOR("Jens Reidel <adrian@travitia.xyz>");
MODULE_DESCRIPTION("DRM panel driver for the Novatek NT36532 Driver-IC");
MODULE_LICENSE("GPL");
