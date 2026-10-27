// SPDX-License-Identifier: GPL-2.0-only
/*
 * Novatek NT36523 DriverIC panels driver
 *
 * Copyright (c) 2022, 2023 Jianhua Lu <lujianhua000@gmail.com>
 */

#include <linux/backlight.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>
#include <linux/seq_file.h>
#include <linux/string.h>

#include <video/mipi_display.h>

#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#define DSI_NUM_MIN 1

struct panel_info {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi[2];
	const struct panel_desc *desc;
	enum drm_panel_orientation orientation;

	struct gpio_desc *reset_gpio;
	struct backlight_device *backlight;
	struct regulator_bulk_data supplies[3];
	unsigned int num_supplies;
	struct drm_dsc_config dsc;
	struct dentry *debugfs;
};

struct panel_desc {
	unsigned int width_mm;
	unsigned int height_mm;

	unsigned int bpc;
	unsigned int lanes;
	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;

	const struct drm_display_mode *modes;
	unsigned int num_modes;
	const struct mipi_dsi_device_info dsi_info;
	int (*init_sequence)(struct panel_info *pinfo);

	bool is_dual_dsi;
	bool has_dcs_backlight;

	unsigned int num_supplies;
	const char *const *supply_names;
	const struct drm_dsc_config *dsc;
	unsigned int dsc_slice_per_pkt;
	void (*reset_sequence)(struct panel_info *pinfo);
};

static inline struct panel_info *to_panel_info(struct drm_panel *panel)
{
	return container_of(panel, struct panel_info, panel);
}

static struct mipi_dsi_device *nt36523_cmd_dsi(struct panel_info *pinfo)
{
	return pinfo->dsi[1] ?: pinfo->dsi[0];
}

static int elish_boe_init_sequence(struct panel_info *pinfo)
{
	struct mipi_dsi_device *dsi0 = pinfo->dsi[0];
	struct mipi_dsi_device *dsi1 = pinfo->dsi[1];
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = NULL };
	/* No datasheet, so write magic init sequence directly */
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x10);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xb9, 0x05);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x20);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x18, 0x40);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x10);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xb9, 0x02);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x23);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x00, 0x80);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x01, 0x84);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x05, 0x2d);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x06, 0x00);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x07, 0x00);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x08, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x09, 0x45);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x11, 0x02);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x12, 0x80);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x15, 0x83);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x16, 0x0c);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x29, 0x0a);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x30, 0xff);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x31, 0xfe);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x32, 0xfd);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x33, 0xfb);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x34, 0xf8);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x35, 0xf5);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x36, 0xf3);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x37, 0xf2);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x38, 0xf2);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x39, 0xf2);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x3a, 0xef);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x3b, 0xec);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x3d, 0xe9);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x3f, 0xe5);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x40, 0xe5);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x41, 0xe5);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x2a, 0x13);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x45, 0xff);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x46, 0xf4);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x47, 0xe7);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x48, 0xda);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x49, 0xcd);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x4a, 0xc0);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x4b, 0xb3);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x4c, 0xb2);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x4d, 0xb2);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x4e, 0xb2);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x4f, 0x99);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x50, 0x80);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x51, 0x68);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x52, 0x66);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x53, 0x66);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x54, 0x66);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x2b, 0x0e);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x58, 0xff);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x59, 0xfb);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x5a, 0xf7);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x5b, 0xf3);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x5c, 0xef);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x5d, 0xe3);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x5e, 0xda);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x5f, 0xd8);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x60, 0xd8);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x61, 0xd8);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x62, 0xcb);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x63, 0xbf);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x64, 0xb3);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x65, 0xb2);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x66, 0xb2);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x67, 0xb2);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x2a);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x25, 0x47);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x30, 0x47);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x39, 0x47);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x26);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x19, 0x10);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x1a, 0xe0);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x1b, 0x10);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x1c, 0x00);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x2a, 0x10);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x2b, 0xe0);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x10);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0xf0);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x84, 0x08);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x85, 0x0c);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x20);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x51, 0x00);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x25);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x91, 0x1f);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x92, 0x0f);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x93, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x94, 0x18);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x95, 0x03);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x96, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x10);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xb0, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x25);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x19, 0x1f);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x1b, 0x1b);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x24);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xb8, 0x28);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x27);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xd0, 0x31);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xd1, 0x20);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xd2, 0x30);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xd4, 0x08);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xde, 0x80);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xdf, 0x02);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x26);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x00, 0x81);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x01, 0xb0);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x22);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x9f, 0x50);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x6f, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x70, 0x11);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x73, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x74, 0x49);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x76, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x77, 0x49);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xa0, 0x3f);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xa9, 0x50);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xaa, 0x28);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xab, 0x28);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xad, 0x10);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xb8, 0x00);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xb9, 0x49);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xba, 0x49);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xbb, 0x49);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xbe, 0x04);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xbf, 0x49);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xc0, 0x04);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xc1, 0x59);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xc2, 0x00);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xc5, 0x00);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xc6, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xc7, 0x48);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xca, 0x43);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xcb, 0x3c);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xce, 0x00);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xcf, 0x43);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xd0, 0x3c);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xd3, 0x43);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xd4, 0x3c);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xd7, 0x00);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xdc, 0x43);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xdd, 0x3c);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xe1, 0x43);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xe2, 0x3c);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xf2, 0x00);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xf3, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xf4, 0x48);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x25);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x13, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x14, 0x23);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xbc, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xbd, 0x23);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x2a);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x97, 0x3c);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x98, 0x02);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x99, 0x95);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x9a, 0x03);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x9b, 0x00);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x9c, 0x0b);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x9d, 0x0a);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x9e, 0x90);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x22);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x9f, 0x50);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x23);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xa3, 0x50);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0xe0);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x14, 0x60);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x16, 0xc0);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x4f, 0x02);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0xf0);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x3a, 0x08);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0xd0);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x02, 0xaf);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x09, 0xee);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x1c, 0x99);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x1d, 0x09);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x10);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x51, 0x0f, 0xff);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x53, 0x2c);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x35, 0x00);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xbb, 0x13);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x3b, 0x03, 0xac, 0x1a, 0x04, 0x04);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x11);
	mipi_dsi_msleep(&dsi_ctx, 70);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x29);

	return dsi_ctx.accum_err;
}

static int elish_csot_init_sequence(struct panel_info *pinfo)
{
	struct mipi_dsi_device *dsi0 = pinfo->dsi[0];
	struct mipi_dsi_device *dsi1 = pinfo->dsi[1];
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = NULL };
	/* No datasheet, so write magic init sequence directly */
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x10);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xb9, 0x05);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x20);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x18, 0x40);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x10);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xb9, 0x02);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0xd0);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x02, 0xaf);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x00, 0x30);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x09, 0xee);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x1c, 0x99);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x1d, 0x09);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0xf0);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x3a, 0x08);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0xe0);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x4f, 0x02);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x20);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x58, 0x40);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x10);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x35, 0x00);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x23);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x00, 0x80);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x01, 0x84);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x05, 0x2d);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x06, 0x00);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x07, 0x00);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x08, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x09, 0x45);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x11, 0x02);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x12, 0x80);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x15, 0x83);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x16, 0x0c);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x29, 0x0a);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x30, 0xff);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x31, 0xfe);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x32, 0xfd);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x33, 0xfb);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x34, 0xf8);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x35, 0xf5);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x36, 0xf3);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x37, 0xf2);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x38, 0xf2);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x39, 0xf2);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x3a, 0xef);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x3b, 0xec);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x3d, 0xe9);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x3f, 0xe5);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x40, 0xe5);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x41, 0xe5);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x2a, 0x13);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x45, 0xff);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x46, 0xf4);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x47, 0xe7);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x48, 0xda);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x49, 0xcd);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x4a, 0xc0);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x4b, 0xb3);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x4c, 0xb2);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x4d, 0xb2);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x4e, 0xb2);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x4f, 0x99);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x50, 0x80);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x51, 0x68);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x52, 0x66);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x53, 0x66);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x54, 0x66);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x2b, 0x0e);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x58, 0xff);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x59, 0xfb);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x5a, 0xf7);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x5b, 0xf3);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x5c, 0xef);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x5d, 0xe3);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x5e, 0xda);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x5f, 0xd8);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x60, 0xd8);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x61, 0xd8);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x62, 0xcb);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x63, 0xbf);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x64, 0xb3);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x65, 0xb2);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x66, 0xb2);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x67, 0xb2);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x10);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x51, 0x0f, 0xff);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x53, 0x2c);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x55, 0x00);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xbb, 0x13);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x3b, 0x03, 0xac, 0x1a, 0x04, 0x04);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x2a);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x25, 0x46);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x30, 0x46);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x39, 0x46);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x26);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x01, 0xb0);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x19, 0x10);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x1a, 0xe0);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x1b, 0x10);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x1c, 0x00);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x2a, 0x10);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x2b, 0xe0);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0xf0);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x84, 0x08);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x85, 0x0c);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x20);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x51, 0x00);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x25);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x91, 0x1f);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x92, 0x0f);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x93, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x94, 0x18);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x95, 0x03);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x96, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x10);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xb0, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x25);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x19, 0x1f);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x1b, 0x1b);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x24);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xb8, 0x28);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x27);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xd0, 0x31);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xd1, 0x20);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xd4, 0x08);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xde, 0x80);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xdf, 0x02);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x26);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x00, 0x81);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x01, 0xb0);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x22);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x6f, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x70, 0x11);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x73, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x74, 0x4d);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xa0, 0x3f);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xa9, 0x50);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xaa, 0x28);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xab, 0x28);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xad, 0x10);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xb8, 0x00);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xb9, 0x4b);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xba, 0x96);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xbb, 0x4b);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xbe, 0x07);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xbf, 0x4b);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xc0, 0x07);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xc1, 0x5c);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xc2, 0x00);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xc5, 0x00);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xc6, 0x3f);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xc7, 0x00);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xca, 0x08);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xcb, 0x40);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xce, 0x00);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xcf, 0x08);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xd0, 0x40);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xd3, 0x08);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xd4, 0x40);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x25);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xbc, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xbd, 0x1c);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x2a);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xfb, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x9a, 0x03);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0xff, 0x10);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x11);
	mipi_dsi_msleep(&dsi_ctx, 70);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, 0x29);

	return dsi_ctx.accum_err;
}

static int j606f_boe_init_sequence(struct panel_info *pinfo)
{
	struct mipi_dsi_device *dsi = pinfo->dsi[0];
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x05, 0xd9);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x07, 0x78);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x08, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0d, 0x63);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0e, 0x91);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0f, 0x73);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x95, 0xeb);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x96, 0xeb);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_PARTIAL_ROWS, 0x11);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6d, 0x66);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x75, 0xa2);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x77, 0xb3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x08, 0x00, 0x23, 0x00, 0x4d, 0x00, 0x6d,
				     0x00, 0x89, 0x00, 0xa1, 0x00, 0xb6, 0x00, 0xc9);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb1, 0x00, 0xda, 0x01, 0x13, 0x01, 0x3c, 0x01, 0x7e,
				     0x01, 0xab, 0x01, 0xf7, 0x02, 0x2f, 0x02, 0x31);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb2, 0x02, 0x67, 0x02, 0xa6, 0x02, 0xd1, 0x03, 0x08,
				     0x03, 0x2e, 0x03, 0x5b, 0x03, 0x6b, 0x03, 0x7b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb3, 0x03, 0x8e, 0x03, 0xa2, 0x03, 0xb7, 0x03, 0xe7,
				     0x03, 0xfd, 0x03, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb4, 0x00, 0x08, 0x00, 0x23, 0x00, 0x4d, 0x00, 0x6d,
				     0x00, 0x89, 0x00, 0xa1, 0x00, 0xb6, 0x00, 0xc9);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb5, 0x00, 0xda, 0x01, 0x13, 0x01, 0x3c, 0x01, 0x7e,
				     0x01, 0xab, 0x01, 0xf7, 0x02, 0x2f, 0x02, 0x31);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb6, 0x02, 0x67, 0x02, 0xa6, 0x02, 0xd1, 0x03, 0x08,
				     0x03, 0x2e, 0x03, 0x5b, 0x03, 0x6b, 0x03, 0x7b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb7, 0x03, 0x8e, 0x03, 0xa2, 0x03, 0xb7, 0x03, 0xe7,
				     0x03, 0xfd, 0x03, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb8, 0x00, 0x08, 0x00, 0x23, 0x00, 0x4d, 0x00, 0x6d,
				     0x00, 0x89, 0x00, 0xa1, 0x00, 0xb6, 0x00, 0xc9);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb9, 0x00, 0xda, 0x01, 0x13, 0x01, 0x3c, 0x01, 0x7e,
				     0x01, 0xab, 0x01, 0xf7, 0x02, 0x2f, 0x02, 0x31);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xba, 0x02, 0x67, 0x02, 0xa6, 0x02, 0xd1, 0x03, 0x08,
				     0x03, 0x2e, 0x03, 0x5b, 0x03, 0x6b, 0x03, 0x7b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbb, 0x03, 0x8e, 0x03, 0xa2, 0x03, 0xb7, 0x03, 0xe7,
				     0x03, 0xfd, 0x03, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x21);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x45, 0x00, 0x65,
				     0x00, 0x81, 0x00, 0x99, 0x00, 0xae, 0x00, 0xc1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb1, 0x00, 0xd2, 0x01, 0x0b, 0x01, 0x34, 0x01, 0x76,
				     0x01, 0xa3, 0x01, 0xef, 0x02, 0x27, 0x02, 0x29);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb2, 0x02, 0x5f, 0x02, 0x9e, 0x02, 0xc9, 0x03, 0x00,
				     0x03, 0x26, 0x03, 0x53, 0x03, 0x63, 0x03, 0x73);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb3, 0x03, 0x86, 0x03, 0x9a, 0x03, 0xaf, 0x03, 0xdf,
				     0x03, 0xf5, 0x03, 0xf7);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb4, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x45, 0x00, 0x65,
				     0x00, 0x81, 0x00, 0x99, 0x00, 0xae, 0x00, 0xc1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb5, 0x00, 0xd2, 0x01, 0x0b, 0x01, 0x34, 0x01, 0x76,
				     0x01, 0xa3, 0x01, 0xef, 0x02, 0x27, 0x02, 0x29);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb6, 0x02, 0x5f, 0x02, 0x9e, 0x02, 0xc9, 0x03, 0x00,
				     0x03, 0x26, 0x03, 0x53, 0x03, 0x63, 0x03, 0x73);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb7, 0x03, 0x86, 0x03, 0x9a, 0x03, 0xaf, 0x03, 0xdf,
				     0x03, 0xf5, 0x03, 0xf7);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb8, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x45, 0x00, 0x65,
				     0x00, 0x81, 0x00, 0x99, 0x00, 0xae, 0x00, 0xc1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb9, 0x00, 0xd2, 0x01, 0x0b, 0x01, 0x34, 0x01, 0x76,
				     0x01, 0xa3, 0x01, 0xef, 0x02, 0x27, 0x02, 0x29);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xba, 0x02, 0x5f, 0x02, 0x9e, 0x02, 0xc9, 0x03, 0x00,
				     0x03, 0x26, 0x03, 0x53, 0x03, 0x63, 0x03, 0x73);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbb, 0x03, 0x86, 0x03, 0x9a, 0x03, 0xaf, 0x03, 0xdf,
				     0x03, 0xf5, 0x03, 0xf7);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x23);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x07, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x11, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x12, 0x77);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x15, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x16, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x01, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x02, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x03, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x04, 0x1d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x05, 0x1d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x06, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x07, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x08, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x09, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0a, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0b, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0c, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0d, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0e, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0f, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x10, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x11, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x12, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x13, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x14, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x15, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x16, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x17, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x18, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x19, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1a, 0x1d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1b, 0x1d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1c, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1d, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1e, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1f, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x20, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x21, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x22, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x23, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x24, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x25, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_GAMMA_CURVE, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x27, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x28, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x29, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2a, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2b, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_LUT, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2f, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_PARTIAL_ROWS, 0x44);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x33, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x34, 0x32);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x37, 0x44);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x38, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x39, 0x00);

	mipi_dsi_dcs_set_pixel_format_multi(&dsi_ctx, 0x9a);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3b, 0xa0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_3D_CONTROL, 0x42);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3f, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x43, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x47, 0x66);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4a, 0x9a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4b, 0xa0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4c, 0x91);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4d, 0x21);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4e, 0x43);

	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 18);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x52, 0x34);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x55, 0x82, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x56, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x58, 0x21);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x59, 0x30);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5a, 0xba);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5b, 0xa0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_CABC_MIN_BRIGHTNESS, 0x00, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5f, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x65, 0x82);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7e, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7f, 0x3c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x82, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x97, 0xc0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb6,
				     0x05, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05,
				     0x05, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x92, 0xc4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x93, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x94, 0x5f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd7, 0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xda, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xde, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdb, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdc, 0xc4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdd, 0x22);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdf, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe0, 0xc4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe1, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe2, 0xc4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe3, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe4, 0xc4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe5, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe6, 0xc4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5c, 0x88);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5d, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8d, 0x88);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8e, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb5, 0x90);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x25);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x05, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x19, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1f, 0xba);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x20, 0xa0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_GAMMA_CURVE, 0xba);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x27, 0xa0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x33, 0xba);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x34, 0xa0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3f, 0xe0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_VSYNC_TIMING, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x44, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_GET_SCANLINE, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x48, 0xba);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x49, 0xa0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5b, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5c, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5d, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_CABC_MIN_BRIGHTNESS, 0xd0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x61, 0xba);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x62, 0xa0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf1, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x2a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x64, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x67, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6a, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x70, 0x30);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_READ_PPS_START, 0xf3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa3, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa4, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa5, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd6, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x26);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0xa1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0a, 0xf2);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x04, 0x28);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x06, 0x30);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0c, 0x13);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0d, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0f, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x11, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x12, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x13, 0x51);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x14, 0x65);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x15, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x16, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x17, 0xa0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x18, 0x86);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x19, 0x11);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1a, 0x7b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1b, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1c, 0xbb);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x22, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x23, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2a, 0x11);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2b, 0x7b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1d, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1e, 0xc3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1f, 0xc3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x24, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x25, 0xc3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2f, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_PARTIAL_ROWS, 0xc3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_PARTIAL_COLUMNS, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x32, 0xc3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x39, 0x00);

	mipi_dsi_dcs_set_pixel_format_multi(&dsi_ctx, 0xc3);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x20, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x33, 0x11);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x34, 0x78);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x35, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc8, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc9, 0x82);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xca, 0x4e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcb, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_READ_PPS_CONTINUE, 0x4c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xaa, 0x47);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x27);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x56, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x58, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x59, 0x53);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5a, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5b, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5c, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5d, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_CABC_MIN_BRIGHTNESS, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5f, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x60, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x61, 0x1d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x62, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x63, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x64, 0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x65, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x66, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x67, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x68, 0x25);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x78, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc3, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd1, 0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd2, 0x30);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x2a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x22, 0x2f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x23, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x24, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x25, 0xc3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_GAMMA_CURVE, 0xf8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x27, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x28, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x29, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2a, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2b, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_LUT, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0xe0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x14, 0x60);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x16, 0xc0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0xf0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);

	mipi_dsi_dcs_set_pixel_format_multi(&dsi_ctx, 0x08);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);

	mipi_dsi_dcs_set_pixel_format_multi(&dsi_ctx, 0x5d);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3b, 0x60);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4a, 0x5d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4b, 0x60);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5a, 0x70);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5b, 0x60);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x91, 0x44);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x92, 0x75);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdb, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdc, 0x75);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdd, 0x22);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdf, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe0, 0x75);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe1, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe2, 0x75);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe3, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe4, 0x75);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe5, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe6, 0x75);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5c, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5d, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8d, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8e, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x25);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1f, 0x70);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x20, 0x60);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_GAMMA_CURVE, 0x70);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x27, 0x60);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x33, 0x70);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x34, 0x60);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x48, 0x70);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x49, 0x60);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5b, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x61, 0x70);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x62, 0x60);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x26);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x02, 0x31);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x19, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1a, 0x7f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1b, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1c, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2a, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2b, 0x7f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1e, 0x75);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1f, 0x75);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x25, 0x75);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_PARTIAL_ROWS, 0x75);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_PARTIAL_COLUMNS, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x32, 0x8d);

	mipi_dsi_dcs_set_pixel_format_multi(&dsi_ctx, 0x75);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x2a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x25, 0x75);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb9, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x18, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb9, 0x02);

	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbb, 0x13);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3b, 0x03, 0x5f, 0x1a, 0x04, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x10);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);

	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x2c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_POWER_SAVE, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x68, 0x05, 0x01);

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 100);

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 30);

	return dsi_ctx.accum_err;
}

static const struct drm_display_mode elish_boe_modes[] = {
	{
		.clock = (1600 + 60 + 8 + 60) * (2560 + 26 + 4 + 168) * 120 / 1000,
		.hdisplay = 1600,
		.hsync_start = 1600 + 60,
		.hsync_end = 1600 + 60 + 8,
		.htotal = 1600 + 60 + 8 + 60,
		.vdisplay = 2560,
		.vsync_start = 2560 + 26,
		.vsync_end = 2560 + 26 + 4,
		.vtotal = 2560 + 26 + 4 + 168,
	},
};

static const struct drm_display_mode elish_csot_modes[] = {
	{
		.clock = (1600 + 200 + 40 + 52) * (2560 + 26 + 4 + 168) * 120 / 1000,
		.hdisplay = 1600,
		.hsync_start = 1600 + 200,
		.hsync_end = 1600 + 200 + 40,
		.htotal = 1600 + 200 + 40 + 52,
		.vdisplay = 2560,
		.vsync_start = 2560 + 26,
		.vsync_end = 2560 + 26 + 4,
		.vtotal = 2560 + 26 + 4 + 168,
	},
};

static const struct drm_display_mode j606f_boe_modes[] = {
	{
		.clock = (1200 + 58 + 2 + 60) * (2000 + 26 + 2 + 93) * 60 / 1000,
		.hdisplay = 1200,
		.hsync_start = 1200 + 58,
		.hsync_end = 1200 + 58 + 2,
		.htotal = 1200 + 58 + 2 + 60,
		.vdisplay = 2000,
		.vsync_start = 2000 + 26,
		.vsync_end = 2000 + 26 + 2,
		.vtotal = 2000 + 26 + 2 + 93,
		.width_mm = 143,
		.height_mm = 235,
	},
};

static const struct panel_desc elish_boe_desc = {
	.modes = elish_boe_modes,
	.num_modes = ARRAY_SIZE(elish_boe_modes),
	.dsi_info = {
		.type = "BOE-elish",
		.channel = 0,
		.node = NULL,
	},
	.width_mm = 127,
	.height_mm = 203,
	.bpc = 8,
	.lanes = 3,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM,
	.init_sequence = elish_boe_init_sequence,
	.is_dual_dsi = true,
};

static const struct panel_desc elish_csot_desc = {
	.modes = elish_csot_modes,
	.num_modes = ARRAY_SIZE(elish_csot_modes),
	.dsi_info = {
		.type = "CSOT-elish",
		.channel = 0,
		.node = NULL,
	},
	.width_mm = 127,
	.height_mm = 203,
	.bpc = 8,
	.lanes = 3,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM,
	.init_sequence = elish_csot_init_sequence,
	.is_dual_dsi = true,
};

static const struct panel_desc j606f_boe_desc = {
	.modes = j606f_boe_modes,
	.num_modes = ARRAY_SIZE(j606f_boe_modes),
	.width_mm = 143,
	.height_mm = 235,
	.bpc = 8,
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
		      MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM,
	.init_sequence = j606f_boe_init_sequence,
	.has_dcs_backlight = true,
};

/* --- Xiaomi Pad 6 Pro (liuqin), dual-DSI NT36532 with DSC --- */

static const char *const pipa_supply_names[] = {
	"vddio", "dvddbuck", "dvddldo",
};

static void pipa_reset(struct panel_info *pinfo)
{
	gpiod_set_value_cansleep(pinfo->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(pinfo->reset_gpio, 1);
	usleep_range(3000, 4000);
	gpiod_set_value_cansleep(pinfo->reset_gpio, 0);
	usleep_range(3000, 4000);
	gpiod_set_value_cansleep(pinfo->reset_gpio, 1);
	usleep_range(15000, 16000);
}

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

static const struct drm_dsc_config pipa_dsc = {
	.dsc_version_major = 1,
	.dsc_version_minor = 1,
	.slice_height = 20,
	.slice_width = 450,
	.slice_count = 2,

	.bits_per_component = 8,
	.bits_per_pixel = 8 << 4, /* 4 fractional bits */
	.block_pred_enable = true,
};

static int pipa_init_sequence(struct panel_info *pinfo)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = nt36523_cmd_dsi(pinfo) };

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

static int m81_36_02_0a_init_sequence(struct panel_info *pinfo)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = nt36523_cmd_dsi(pinfo) };

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

static const struct panel_desc pipa_desc = {
	.modes = &pipa_mode_120,
	.num_modes = 1,
	.width_mm = 148,
	.height_mm = 237,
	.bpc = 8,
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
		MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM,
	.init_sequence = pipa_init_sequence,
	.reset_sequence = pipa_reset,
	.dsi_info = { .type = "pipa", .channel = 0, .node = NULL },
	.num_supplies = 3,
	.supply_names = pipa_supply_names,
	.dsc = &pipa_dsc,
	.dsc_slice_per_pkt = 2,
	.is_dual_dsi = true,
};

/*
 * The Tianma module shares the CSOT node's mode, DSC, clock rate and reset
 * sequence in the vendor DTBO, so it only differs in its 216-packet init
 * sequence.  Both nodes declare burst_mode; the vendor's 144 Hz runtime mode
 * has no captured porch set yet and is not used here.
 */
static const struct panel_desc pipa_tm_desc = {
	.modes = &pipa_mode_120,
	.num_modes = 1,
	.width_mm = 148,
	.height_mm = 237,
	.bpc = 8,
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
		MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM,
	.init_sequence = m81_36_02_0a_init_sequence,
	.reset_sequence = pipa_reset,
	.dsi_info = { .type = "pipa", .channel = 0, .node = NULL },
	.num_supplies = 3,
	.supply_names = pipa_supply_names,
	.dsc = &pipa_dsc,
	.dsc_slice_per_pkt = 2,
	.is_dual_dsi = true,
};

static void nt36523_reset(struct panel_info *pinfo)
{
	gpiod_set_value_cansleep(pinfo->reset_gpio, 1);
	usleep_range(12000, 13000);
	gpiod_set_value_cansleep(pinfo->reset_gpio, 0);
	usleep_range(12000, 13000);
	gpiod_set_value_cansleep(pinfo->reset_gpio, 1);
	usleep_range(12000, 13000);
	gpiod_set_value_cansleep(pinfo->reset_gpio, 0);
	usleep_range(12000, 13000);
}

static int nt36523_prepare(struct drm_panel *panel)
{
	struct panel_info *pinfo = to_panel_info(panel);
	struct drm_dsc_picture_parameter_set pps;
	struct mipi_dsi_device *dsi;
	int ret;

	ret = regulator_bulk_enable(pinfo->num_supplies, pinfo->supplies);
	if (ret) {
		dev_err(panel->dev, "failed to enable regulators: %d\n", ret);
		return ret;
	}

	msleep(120);

	if (pinfo->desc->reset_sequence)
		pinfo->desc->reset_sequence(pinfo);
	else
		nt36523_reset(pinfo);

	msleep(120);

	ret = pinfo->desc->init_sequence(pinfo);
	if (ret < 0) {
		regulator_bulk_disable(pinfo->num_supplies, pinfo->supplies);
		dev_err(panel->dev, "failed to initialize panel: %d\n", ret);
		return ret;
	}

	if (pinfo->desc->dsc) {
		dsi = nt36523_cmd_dsi(pinfo);

		drm_dsc_pps_payload_pack(&pps, &pinfo->dsc);

		dev_info(panel->dev,
			 "dsc: slice %ux%u count %u bpp %u bpc %u chunk %u block_pred %u\n",
			 pinfo->dsc.slice_width, pinfo->dsc.slice_height,
			 pinfo->dsc.slice_count, pinfo->dsc.bits_per_pixel,
			 pinfo->dsc.bits_per_component, pinfo->dsc.slice_chunk_size,
			 pinfo->dsc.block_pred_enable);
		dev_info(panel->dev, "pps: %*ph\n", (int)sizeof(pps), &pps);

		ret = mipi_dsi_picture_parameter_set(dsi, &pps);
		if (ret < 0) {
			regulator_bulk_disable(pinfo->num_supplies, pinfo->supplies);
			dev_err(panel->dev, "failed to transmit PPS: %d\n", ret);
			return ret;
		}

		ret = mipi_dsi_compression_mode(dsi, true);
		if (ret < 0) {
			regulator_bulk_disable(pinfo->num_supplies, pinfo->supplies);
			dev_err(panel->dev, "failed to enable compression mode: %d\n", ret);
			return ret;
		}

		msleep(120);
	}

	return 0;
}

static int nt36523_disable(struct drm_panel *panel)
{
	struct panel_info *pinfo = to_panel_info(panel);
	int i;

	for (i = 0; i < DSI_NUM_MIN + pinfo->desc->is_dual_dsi; i++) {
		struct mipi_dsi_multi_context dsi_ctx = { .dsi = pinfo->dsi[i]};

		mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	}

	for (i = 0; i < DSI_NUM_MIN + pinfo->desc->is_dual_dsi; i++) {
		struct mipi_dsi_multi_context dsi_ctx = { .dsi = pinfo->dsi[i]};

		mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	}

	msleep(70);

	return 0;
}

static int nt36523_unprepare(struct drm_panel *panel)
{
	struct panel_info *pinfo = to_panel_info(panel);

	gpiod_set_value_cansleep(pinfo->reset_gpio, 1);
	regulator_bulk_disable(pinfo->num_supplies, pinfo->supplies);

	return 0;
}

static void nt36523_remove(struct mipi_dsi_device *dsi)
{
	struct panel_info *pinfo = mipi_dsi_get_drvdata(dsi);

	debugfs_remove_recursive(pinfo->debugfs);
	drm_panel_remove(&pinfo->panel);
}

static int nt36523_get_modes(struct drm_panel *panel,
			       struct drm_connector *connector)
{
	struct panel_info *pinfo = to_panel_info(panel);
	int i;

	for (i = 0; i < pinfo->desc->num_modes; i++) {
		const struct drm_display_mode *m = &pinfo->desc->modes[i];
		struct drm_display_mode *mode;

		mode = drm_mode_duplicate(connector->dev, m);
		if (!mode) {
			dev_err(panel->dev, "failed to add mode %ux%u@%u\n",
				m->hdisplay, m->vdisplay, drm_mode_vrefresh(m));
			return -ENOMEM;
		}

		mode->type = DRM_MODE_TYPE_DRIVER;
		if (i == 0)
			mode->type |= DRM_MODE_TYPE_PREFERRED;

		drm_mode_set_name(mode);
		drm_mode_probed_add(connector, mode);
	}

	connector->display_info.width_mm = pinfo->desc->width_mm;
	connector->display_info.height_mm = pinfo->desc->height_mm;
	connector->display_info.bpc = pinfo->desc->bpc;

	return pinfo->desc->num_modes;
}

static enum drm_panel_orientation nt36523_get_orientation(struct drm_panel *panel)
{
	struct panel_info *pinfo = to_panel_info(panel);

	return pinfo->orientation;
}

static const struct drm_panel_funcs nt36523_panel_funcs = {
	.disable = nt36523_disable,
	.prepare = nt36523_prepare,
	.unprepare = nt36523_unprepare,
	.get_modes = nt36523_get_modes,
	.get_orientation = nt36523_get_orientation,
};

static int nt36523_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static int nt36523_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness_large(dsi, &brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return brightness;
}

static const struct backlight_ops nt36523_bl_ops = {
	.update_status = nt36523_bl_update_status,
	.get_brightness = nt36523_bl_get_brightness,
};

static struct backlight_device *nt36523_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 512,
		.max_brightness = 4095,
		.scale = BACKLIGHT_SCALE_NON_LINEAR,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &nt36523_bl_ops, &props);
}

/*
 * Read-only panel state, for the black-panel investigation.  The panel is the
 * only channel on this tablet, so the moment the kernel takes the DSI link over
 * from the bootloader cannot be watched on the glass.  Sampling the panel's
 * own status afterwards separates the two remaining cases: a panel that scans
 * the frames we send (scanline advances, signal mode is video) and therefore
 * shows the wrong picture, from a panel that never locks to the stream and
 * stays in idle mode.
 */
static int nt36523_state_show(struct seq_file *s, void *unused)
{
	struct panel_info *pinfo = s->private;
	const struct drm_display_mode *mode = &pinfo->desc->modes[0];
	struct mipi_dsi_device *dsi = nt36523_cmd_dsi(pinfo);
	u8 power = 0xff, signal = 0xff, display = 0xff;
	u8 line = 0, first = 0, last = 0;
	bool advancing = false;
	int i, ret;

	ret = mipi_dsi_dcs_read(dsi, MIPI_DCS_GET_POWER_MODE, &power, 1);
	if (ret < 0) {
		seq_printf(s, "panel did not answer DCS 0x0a: %d\n", ret);
		return 0;
	}

	if (mipi_dsi_dcs_read(dsi, MIPI_DCS_GET_SIGNAL_MODE, &signal, 1) < 0)
		signal = 0xff;
	if (mipi_dsi_dcs_read(dsi, MIPI_DCS_GET_DISPLAY_MODE, &display, 1) < 0)
		display = 0xff;

	for (i = 0; i < 16; i++) {
		ret = mipi_dsi_dcs_read(dsi, MIPI_DCS_GET_SCANLINE, &line, 1);
		if (ret < 0) {
			seq_printf(s, "scanline read failed: %d\n", ret);
			break;
		}
		if (i == 0)
			first = line;
		if (i > 0 && line != last)
			advancing = true;
		last = line;
		msleep(20);
	}

	seq_printf(s, "power=0x%02x signal=0x%02x display=0x%02x\n",
		   power, signal, display);
	seq_printf(s, "scanline first=%u last=%u advancing=%s\n",
		   first, last, advancing ? "yes" : "no");
	seq_printf(s, "mode=%ux%u@%u htotal=%u vtotal=%u\n",
		   mode->hdisplay, mode->vdisplay, drm_mode_vrefresh(mode),
		   mode->htotal, mode->vtotal);
	seq_printf(s, "links=%s lanes=%u dsc=%s\n",
		   pinfo->dsi[1] ? "dual" : "single", dsi->lanes,
		   pinfo->desc->dsc ? "on" : "off");

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(nt36523_state);

static int nt36523_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct device_node *dsi1;
	struct mipi_dsi_host *dsi1_host;
	struct panel_info *pinfo;
	const struct mipi_dsi_device_info *info;
	int i, ret;

	pinfo = devm_drm_panel_alloc(dev, struct panel_info, panel,
				     &nt36523_panel_funcs,
				     DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(pinfo))
		return PTR_ERR(pinfo);

	pinfo->desc = of_device_get_match_data(dev);
	if (!pinfo->desc)
		return -ENODEV;

	/*
	 * liuqin ships with one of two NT36532 modules and their init sequences
	 * are not interchangeable.  The bootloader names the fitted module on
	 * the kernel command line (msm_drm.dsi_display0=...), so pick the
	 * matching descriptor from there.  Known modules: m81_36_02_0a ->
	 * Tianma, m81_42_02_0b -> CSOT.
	 */
	if (pinfo->desc == &pipa_desc && saved_command_line &&
	    strstr(saved_command_line, "mdss_dsi_m81_36_02_0a"))
		pinfo->desc = &pipa_tm_desc;

	pinfo->num_supplies = pinfo->desc->num_supplies ?: 1;
	for (i = 0; i < pinfo->num_supplies; i++)
		pinfo->supplies[i].supply = pinfo->desc->supply_names ?
			pinfo->desc->supply_names[i] : "vddio";

	ret = devm_regulator_bulk_get(dev, pinfo->num_supplies,
				      pinfo->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get regulators\n");

	pinfo->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(pinfo->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(pinfo->reset_gpio), "failed to get reset gpio\n");

	if (pinfo->desc->dsc)
		memcpy(&pinfo->dsc, pinfo->desc->dsc, sizeof(pinfo->dsc));

	/* If the panel is dual dsi, register DSI1 */
	if (pinfo->desc->is_dual_dsi) {
		info = &pinfo->desc->dsi_info;

		dsi1 = of_graph_get_remote_node(dsi->dev.of_node, 1, -1);
		if (!dsi1) {
			dev_err(dev, "cannot get secondary DSI node.\n");
			return -ENODEV;
		}

		dsi1_host = of_find_mipi_dsi_host_by_node(dsi1);
		of_node_put(dsi1);
		if (!dsi1_host)
			return dev_err_probe(dev, -EPROBE_DEFER, "cannot get secondary DSI host\n");

		pinfo->dsi[1] = devm_mipi_dsi_device_register_full(dev, dsi1_host, info);
		if (IS_ERR(pinfo->dsi[1])) {
			dev_err(dev, "cannot get secondary DSI device\n");
			return PTR_ERR(pinfo->dsi[1]);
		}
	}

	pinfo->dsi[0] = dsi;
	mipi_dsi_set_drvdata(dsi, pinfo);

	if (of_drm_get_panel_orientation(dev->of_node, &pinfo->orientation) < 0)
		pinfo->orientation = DRM_MODE_PANEL_ORIENTATION_NORMAL;

	pinfo->panel.prepare_prev_first = true;

	if (pinfo->desc->has_dcs_backlight) {
		pinfo->panel.backlight = nt36523_create_backlight(dsi);
		if (IS_ERR(pinfo->panel.backlight))
			return dev_err_probe(dev, PTR_ERR(pinfo->panel.backlight),
					     "Failed to create backlight\n");
	} else {
		ret = drm_panel_of_backlight(&pinfo->panel);
		if (ret)
			return dev_err_probe(dev, ret, "Failed to get backlight\n");
	}

	drm_panel_add(&pinfo->panel);

	for (i = 0; i < DSI_NUM_MIN + pinfo->desc->is_dual_dsi; i++) {
		pinfo->dsi[i]->lanes = pinfo->desc->lanes;
		pinfo->dsi[i]->format = pinfo->desc->format;
		pinfo->dsi[i]->mode_flags = pinfo->desc->mode_flags;
		if (pinfo->desc->dsc) {
			pinfo->dsi[i]->dsc = &pinfo->dsc;
			pinfo->dsi[i]->dsc_slice_per_pkt =
				pinfo->desc->dsc_slice_per_pkt;
		}

		ret = devm_mipi_dsi_attach(dev, pinfo->dsi[i]);
		if (ret < 0)
			return dev_err_probe(dev, ret, "cannot attach to DSI%d host.\n", i);
	}

	pinfo->debugfs = debugfs_create_dir("nt36523", NULL);
	if (!IS_ERR_OR_NULL(pinfo->debugfs))
		debugfs_create_file("state", 0400, pinfo->debugfs, pinfo,
				    &nt36523_state_fops);

	return 0;
}

static const struct of_device_id nt36523_of_match[] = {
	{
		.compatible = "lenovo,j606f-boe-nt36523w",
		.data = &j606f_boe_desc,
	},
	{
		.compatible = "xiaomi,elish-boe-nt36523",
		.data = &elish_boe_desc,
	},
	{
		.compatible = "xiaomi,elish-csot-nt36523",
		.data = &elish_csot_desc,
	},
	{
		.compatible = "xiaomi,pipa-nt36532",
		.data = &pipa_desc,
	},
	{},
};
MODULE_DEVICE_TABLE(of, nt36523_of_match);

static struct mipi_dsi_driver nt36523_driver = {
	.probe = nt36523_probe,
	.remove = nt36523_remove,
	.driver = {
		.name = "panel-novatek-nt36523",
		.of_match_table = nt36523_of_match,
	},
};
module_mipi_dsi_driver(nt36523_driver);

MODULE_AUTHOR("Jianhua Lu <lujianhua000@gmail.com>");
MODULE_DESCRIPTION("DRM driver for Novatek NT36523 based MIPI DSI panels");
MODULE_LICENSE("GPL");
