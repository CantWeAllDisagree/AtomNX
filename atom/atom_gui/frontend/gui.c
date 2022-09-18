﻿/*
 * 
 * Copyright (c) 2018-2022 CTCaer
 * Copyright (c) 2020 Storm
 * Copyright (c) 2022 CantWeAllDisagree ⚛
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>

#include <bdk.h>

#include "gui.h"
#include "gui_emummc_tools.h"
#include "gui_tools.h"
#include "gui_info.h"
#include "gui_options.h"
#include <libs/lvgl/lvgl.h>
#include <libs/lvgl/lv_objx/lv_kb.c>
#include "../gfx/logos-gui.h"

#include <utils/btn.h>

#include "../config.h"
#include <libs/fatfs/ff.h>

extern hekate_config h_cfg;
extern nyx_config n_cfg;
extern volatile boot_cfg_t *b_cfg;
extern volatile nyx_storage_t *nyx_str;

extern lv_res_t launch_payload(lv_obj_t *list);
extern lv_res_t launch_payload_btn(lv_obj_t* obj);// Payload Loader Label Dumb AF

static bool disp_init_done = false;
static bool do_reload = false;

lv_style_t hint_small_style;
lv_style_t hint_small_style_white;
lv_style_t monospace_text;

lv_obj_t *payload_list;
lv_obj_t *autorcm_btn;
lv_obj_t *close_btn;
lv_obj_t* close_firstwin;// Static Close Button for first window

lv_img_dsc_t *icon_switch;
lv_img_dsc_t *icon_payload;
lv_img_dsc_t *icon_lakka;

lv_img_dsc_t *hekate_bg;

// Static Default Styles
lv_style_t mbox_darken;
lv_style_t hint_small_style;
lv_style_t hint_small_style_white;
lv_style_t monospace_text;

lv_style_t btn_transp_rel, btn_transp_pr;
lv_style_t tabview_btn_pr, tabview_btn_tgl_pr;

lv_style_t header_style;
lv_style_t win_bg_style;
lv_style_t style_kb_rel;
lv_style_t style_kb_pr;

lv_style_t font20_style;
lv_style_t font20red_style;
lv_style_t font20green_style;
lv_style_t labels_style;
lv_style_t inv_label;

char *text_color;

typedef struct _jc_lv_driver_t
{
	lv_indev_t *indev;
	bool centering_done;
	u16 cx_max;
	u16 cx_min;
	u16 cy_max;
	u16 cy_min;
	s16 pos_x;
	s16 pos_y;
	s16 pos_last_x;
	s16 pos_last_y;
	lv_obj_t *cursor;
	u32 cursor_timeout;
	bool cursor_hidden;
	u32 console_timeout;
} jc_lv_driver_t;

static jc_lv_driver_t jc_drv_ctx;
// Define Status Bar
gui_status_bar_ctx status_bar;

static void _nyx_disp_init()
{
	display_backlight_brightness(0, 1000);
	display_init_framebuffer_pitch_inv();
	display_init_framebuffer_log();
	display_backlight_brightness(h_cfg.backlight - 20, 1000);
}

static void _save_log_to_bmp(char *fname)
{
	u32 *fb_ptr = (u32 *)LOG_FB_ADDRESS;

	// Check if there's log written.
	bool log_changed = false;
	for (u32 i = 0; i < 0xCD000; i++)
	{
		if (fb_ptr[i] != 0)
		{
			log_changed = true;
			break;
		}
	}

	if (!log_changed)
		return;

	const u32 file_size = 0x334000 + 0x36;
	u8 *bitmap = malloc(file_size);

	// Reconstruct FB for bottom-top, landscape bmp.
	u32 *fb = malloc(0x334000);
	for (int x = 1279; x > - 1; x--)
	{
		for (int y = 655; y > -1; y--)
			fb[y * 1280 + x] = *fb_ptr++;
	}

	manual_system_maintenance(true);

	memcpy(bitmap + 0x36, fb, 0x334000);

	typedef struct _bmp_t
	{
		u16 magic;
		u32 size;
		u32 rsvd;
		u32 data_off;
		u32 hdr_size;
		u32 width;
		u32 height;
		u16 planes;
		u16 pxl_bits;
		u32 comp;
		u32 img_size;
		u32 res_h;
		u32 res_v;
		u64 rsvd2;
	} __attribute__((packed)) bmp_t;

	bmp_t *bmp = (bmp_t *)bitmap;

	bmp->magic    = 0x4D42;
	bmp->size     = file_size;
	bmp->rsvd     = 0;
	bmp->data_off = 0x36;
	bmp->hdr_size = 40;
	bmp->width    = 1280;
	bmp->height   = 656;
	bmp->planes   = 1;
	bmp->pxl_bits = 32;
	bmp->comp     = 0;
	bmp->img_size = 0x334000;
	bmp->res_h    = 2834;
	bmp->res_v    = 2834;
	bmp->rsvd2    = 0;

	char path[0x80];
	strcpy(path, "AtomNX/screenshots");
	s_printf(path + strlen(path), "/atom%s_log.bmp", fname);
	sd_save_to_file(bitmap, file_size, path);

	free(bitmap);
	free(fb);
}

static void _save_fb_to_bmp()
{
	// Disallow screenshots if less than 2s passed.
	static u32 timer = 0;
	if (get_tmr_ms() < timer)
		return;

	if (do_reload)
		return;

	const u32 file_size = 0x384000 + 0x36;
	u8 *bitmap = malloc(file_size);
	u32 *fb = malloc(0x384000);
	u32 *fb_ptr = (u32 *)NYX_FB_ADDRESS;

	// Reconstruct FB for bottom-top, landscape bmp.
	for (u32 x = 0; x < 1280; x++)
	{
		for (int y = 719; y > -1; y--)
			fb[y * 1280 + x] = *fb_ptr++;
	}

	// Create notification box.
	lv_obj_t * mbox = lv_mbox_create(lv_layer_top(), NULL);
	lv_mbox_set_recolor_text(mbox, true);
	lv_mbox_set_text(mbox, SYMBOL_CAMERA"  #FFDD00 Saving screenshot...#");
	lv_obj_set_width(mbox, LV_DPI * 4);
	lv_obj_set_top(mbox, true);
	lv_obj_align(mbox, NULL, LV_ALIGN_IN_TOP_LEFT, 0, 0);

	// Capture effect.
	display_backlight_brightness(255, 100);
	msleep(150);
	display_backlight_brightness(h_cfg.backlight - 20, 100);

	manual_system_maintenance(true);

	memcpy(bitmap + 0x36, fb, 0x384000);

	typedef struct _bmp_t
	{
		u16 magic;
		u32 size;
		u32 rsvd;
		u32 data_off;
		u32 hdr_size;
		u32 width;
		u32 height;
		u16 planes;
		u16 pxl_bits;
		u32 comp;
		u32 img_size;
		u32 res_h;
		u32 res_v;
		u64 rsvd2;
	} __attribute__((packed)) bmp_t;

	bmp_t *bmp = (bmp_t *)bitmap;

	bmp->magic    = 0x4D42;
	bmp->size     = file_size;
	bmp->rsvd     = 0;
	bmp->data_off = 0x36;
	bmp->hdr_size = 40;
	bmp->width    = 1280;
	bmp->height   = 720;
	bmp->planes   = 1;
	bmp->pxl_bits = 32;
	bmp->comp     = 0;
	bmp->img_size = 0x384000;
	bmp->res_h    = 2834;
	bmp->res_v    = 2834;
	bmp->rsvd2    = 0;

	sd_mount();

	char path[0x80];

	strcpy(path, "AtomNX");
	f_mkdir(path);
	strcat(path, "/screenshots");
	f_mkdir(path);

	// Create date/time name.
	char fname[32];
	rtc_time_t time;
	max77620_rtc_get_time(&time);
	if (n_cfg.timeoff)
	{
		u32 epoch = max77620_rtc_date_to_epoch(&time) + (s32)n_cfg.timeoff;
		max77620_rtc_epoch_to_date(epoch, &time);
	}
	s_printf(fname, "%04d%02d%02d_%02d%02d%02d", time.year, time.month, time.day, time.hour, time.min, time.sec);
	s_printf(path + strlen(path), "/atom%s.bmp", fname);

	// Save screenshot and log.
	int res = sd_save_to_file(bitmap, file_size, path);
	if (!res)
		_save_log_to_bmp(fname);

	sd_unmount();

	free(bitmap);
	free(fb);

	if (!res)
		lv_mbox_set_text(mbox, SYMBOL_CAMERA"  #96FF00 Screenshot saved!#");
	else
		lv_mbox_set_text(mbox, SYMBOL_WARNING"  #FFDD00 Screenshot failed!#");
	manual_system_maintenance(true);
	lv_mbox_start_auto_close(mbox, 4000);

	// Set timer to 2s.
	timer = get_tmr_ms() + 2000;
}

static void _disp_fb_flush(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const lv_color_t *color_p)
{
	// Draw to framebuffer.
	gfx_set_rect_land_pitch((u32 *)NYX_FB_ADDRESS, (u32 *)color_p, 720, x1, y1, x2, y2); //pitch

	// Check if display init was done. If it's the first big draw, init.
	if (!disp_init_done && ((x2 - x1 + 1) > 600))
	{
		disp_init_done = true;
		_nyx_disp_init();
	}

	lv_flush_ready();
}

static touch_event touchpad;
static bool touch_enabled;
static bool console_enabled = false;

static bool _fts_touch_read(lv_indev_data_t *data)
{
	if (touch_enabled)
		touch_poll(&touchpad);
	else
		return false;

	// Take a screenshot if 3 fingers.
	if (touchpad.fingers > 2)
	{
		_save_fb_to_bmp();

		data->state = LV_INDEV_STATE_REL;
		return false;
	}

	if (console_enabled)
	{
		// Print input debugging in console.
		gfx_con_getpos(&gfx_con.savedx, &gfx_con.savedy);
		gfx_con_setpos(32, 638);
		gfx_con.fntsz = 8;
		gfx_printf("x: %4d, y: %4d | z: %3d | ", touchpad.x, touchpad.y, touchpad.z);
		gfx_printf("1: %02x, 2: %02x, 3: %02x, ", touchpad.raw[1], touchpad.raw[2], touchpad.raw[3]);
		gfx_printf("4: %02X, 5: %02x, 6: %02x, 7: %02x",
			touchpad.raw[4], touchpad.raw[5], touchpad.raw[6], touchpad.raw[7]);
		gfx_con_setpos(gfx_con.savedx, gfx_con.savedy);
		gfx_con.fntsz = 16;

		return false;
	}

	// Always set touch points.
	data->point.x = touchpad.x;
	data->point.y = touchpad.y;

	// Decide touch enable.
	switch (touchpad.type & STMFTS_MASK_EVENT_ID)
	{
	case STMFTS_EV_MULTI_TOUCH_ENTER:
	case STMFTS_EV_MULTI_TOUCH_MOTION:
		data->state = LV_INDEV_STATE_PR;
		break;
	case STMFTS_EV_MULTI_TOUCH_LEAVE:
		data->state = LV_INDEV_STATE_REL;
		break;
	case STMFTS_EV_NO_EVENT:
	default:
		if (touchpad.touch)
			data->state = LV_INDEV_STATE_PR;
		else
			data->state = LV_INDEV_STATE_REL;
		break;
	}

	return false; // No buffering so no more data read.
}

static bool _jc_virt_mouse_read(lv_indev_data_t *data)
{
	// Poll Joy-Con.
	jc_gamepad_rpt_t *jc_pad = joycon_poll();

	if (!jc_pad)
	{
		data->state = LV_INDEV_STATE_REL;
		return false;
	}

	// Take a screenshot if Capture button is pressed.
	if (jc_pad->cap)
	{
		_save_fb_to_bmp();

		data->state = LV_INDEV_STATE_REL;
		return false;
	}

	// Calibrate left stick.
	if (!jc_drv_ctx.centering_done)
	{
		if (n_cfg.jc_force_right)
		{
			if (jc_pad->conn_r
				&& jc_pad->rstick_x > 0x400 && jc_pad->rstick_y > 0x400
				&& jc_pad->rstick_x < 0xC00 && jc_pad->rstick_y < 0xC00)
			{
				jc_drv_ctx.cx_max = jc_pad->rstick_x + 0x96;
				jc_drv_ctx.cx_min = jc_pad->rstick_x - 0x96;
				jc_drv_ctx.cy_max = jc_pad->rstick_y + 0x96;
				jc_drv_ctx.cy_min = jc_pad->rstick_y - 0x96;
				jc_drv_ctx.centering_done = true;
				jc_drv_ctx.cursor_timeout = 0;
			}
		}
		else if (jc_pad->conn_l
			     && jc_pad->lstick_x > 0x400 && jc_pad->lstick_y > 0x400
			     && jc_pad->lstick_x < 0xC00 && jc_pad->lstick_y < 0xC00)
		{
			jc_drv_ctx.cx_max = jc_pad->lstick_x + 0x96;
			jc_drv_ctx.cx_min = jc_pad->lstick_x - 0x96;
			jc_drv_ctx.cy_max = jc_pad->lstick_y + 0x96;
			jc_drv_ctx.cy_min = jc_pad->lstick_y - 0x96;
			jc_drv_ctx.centering_done = true;
			jc_drv_ctx.cursor_timeout = 0;
		}
		else
		{
			data->state = LV_INDEV_STATE_REL;
			return false;
		}
	}

	// Re-calibrate on disconnection.
	if (n_cfg.jc_force_right)
	{
		if (!jc_pad->conn_r)
			jc_drv_ctx.centering_done = 0;
	}
	else if (!jc_pad->conn_l)
		jc_drv_ctx.centering_done = 0;

	// Set button presses.
	if (jc_pad->a || jc_pad->zl || jc_pad->zr)
		data->state = LV_INDEV_STATE_PR;
	else
		data->state = LV_INDEV_STATE_REL;

	// Enable console.
	if (jc_pad->plus || jc_pad->minus)
	{
		if (((u32)get_tmr_ms() - jc_drv_ctx.console_timeout) > 1000)
		{
			if (!console_enabled)
			{
				display_activate_console();
				console_enabled = true;
				gfx_con_getpos(&gfx_con.savedx, &gfx_con.savedy);
				gfx_con_setpos(964, 630);
				gfx_printf("Press -/+ to close");
				gfx_con_setpos(gfx_con.savedx, gfx_con.savedy);
			}
			else
			{
				display_deactivate_console();
				console_enabled = false;
			}

			jc_drv_ctx.console_timeout = get_tmr_ms();
		}

		data->state = LV_INDEV_STATE_REL;
		return false;
	}

	if (console_enabled)
	{
		// Print input debugging in console.
		gfx_con_getpos(&gfx_con.savedx, &gfx_con.savedy);
		gfx_con_setpos(32, 630);
		gfx_con.fntsz = 8;
		gfx_printf("x: %4X, y: %4X | b: %06X | bt: %d %d | cx: %03X - %03x, cy: %03X - %03x",
			jc_pad->lstick_x, jc_pad->lstick_y, jc_pad->buttons,
			jc_pad->batt_info_l, jc_pad->batt_info_r,
			jc_drv_ctx.cx_min, jc_drv_ctx.cx_max, jc_drv_ctx.cy_min, jc_drv_ctx.cy_max);
		gfx_con_setpos(gfx_con.savedx, gfx_con.savedy);
		gfx_con.fntsz = 16;

		data->state = LV_INDEV_STATE_REL;
		return false;
	}

	// Calculate new cursor position.
	if (!n_cfg.jc_force_right)
	{
		// Left stick X.
		if (jc_pad->lstick_x <= jc_drv_ctx.cx_max && jc_pad->lstick_x >= jc_drv_ctx.cx_min)
			jc_drv_ctx.pos_x += 0;
		else if (jc_pad->lstick_x > jc_drv_ctx.cx_max)
			jc_drv_ctx.pos_x += ((jc_pad->lstick_x - jc_drv_ctx.cx_max) / 30);
		else
			jc_drv_ctx.pos_x -= ((jc_drv_ctx.cx_min - jc_pad->lstick_x) / 30);

		// Left stick Y.
		if (jc_pad->lstick_y <= jc_drv_ctx.cy_max && jc_pad->lstick_y >= jc_drv_ctx.cy_min)
			jc_drv_ctx.pos_y += 0;
		else if (jc_pad->lstick_y > jc_drv_ctx.cy_max)
		{
			s16 val = (jc_pad->lstick_y - jc_drv_ctx.cy_max) / 30;
			// Hoag has inverted Y axis.
			if (jc_pad->sio_mode)
				val *= -1;
			jc_drv_ctx.pos_y -= val;
		}
		else
		{
			s16 val = (jc_drv_ctx.cy_min - jc_pad->lstick_y) / 30;
			// Hoag has inverted Y axis.
			if (jc_pad->sio_mode)
				val *= -1;
			jc_drv_ctx.pos_y += val;
		}
	}
	else
	{
		// Right stick X.
		if (jc_pad->rstick_x <= jc_drv_ctx.cx_max && jc_pad->rstick_x >= jc_drv_ctx.cx_min)
			jc_drv_ctx.pos_x += 0;
		else if (jc_pad->rstick_x > jc_drv_ctx.cx_max)
			jc_drv_ctx.pos_x += ((jc_pad->rstick_x - jc_drv_ctx.cx_max) / 30);
		else
			jc_drv_ctx.pos_x -= ((jc_drv_ctx.cx_min - jc_pad->rstick_x) / 30);

		// Right stick Y.
		if (jc_pad->rstick_y <= jc_drv_ctx.cy_max && jc_pad->rstick_y >= jc_drv_ctx.cy_min)
			jc_drv_ctx.pos_y += 0;
		else if (jc_pad->rstick_y > jc_drv_ctx.cy_max)
		{
			s16 val = (jc_pad->rstick_y - jc_drv_ctx.cy_max) / 30;
			// Hoag has inverted Y axis.
			if (jc_pad->sio_mode)
				val *= -1;
			jc_drv_ctx.pos_y -= val;
		}
		else
		{
			s16 val = (jc_drv_ctx.cy_min - jc_pad->rstick_y) / 30;
			// Hoag has inverted Y axis.
			if (jc_pad->sio_mode)
				val *= -1;
			jc_drv_ctx.pos_y += val;
		}
	}

	// Ensure value inside screen limits.
	if (jc_drv_ctx.pos_x < 0)
		jc_drv_ctx.pos_x = 0;
	else if (jc_drv_ctx.pos_x > 1279)
		jc_drv_ctx.pos_x = 1279;

	if (jc_drv_ctx.pos_y < 0)
		jc_drv_ctx.pos_y = 0;
	else if (jc_drv_ctx.pos_y > 719)
		jc_drv_ctx.pos_y = 719;

	// Set cursor position.
	data->point.x = jc_drv_ctx.pos_x;
	data->point.y = jc_drv_ctx.pos_y;

	// Auto hide cursor.
	if (jc_drv_ctx.pos_x != jc_drv_ctx.pos_last_x || jc_drv_ctx.pos_y != jc_drv_ctx.pos_last_y)
	{
		jc_drv_ctx.pos_last_x = jc_drv_ctx.pos_x;
		jc_drv_ctx.pos_last_y = jc_drv_ctx.pos_y;

		jc_drv_ctx.cursor_hidden = false;
		jc_drv_ctx.cursor_timeout = get_tmr_ms();
		lv_indev_set_cursor(jc_drv_ctx.indev, jc_drv_ctx.cursor);

		// Show cursor.
		lv_obj_set_opa_scale_enable(jc_drv_ctx.cursor, false);
	}
	else
	{
		if (!jc_drv_ctx.cursor_hidden)
		{
			if (((u32)get_tmr_ms() - jc_drv_ctx.cursor_timeout) > 3000)
			{
				// Remove cursor and hide it.
				lv_indev_set_cursor(jc_drv_ctx.indev, NULL);
				lv_obj_set_opa_scale_enable(jc_drv_ctx.cursor, true);
				lv_obj_set_opa_scale(jc_drv_ctx.cursor, LV_OPA_TRANSP);

				jc_drv_ctx.cursor_hidden = true;
			}
		}
		else
			data->state = LV_INDEV_STATE_REL; // Ensure that no clicks are allowed.
	}
	
	// Button Joycon Close function 
	if (jc_pad->b && close_btn)
	{
		lv_action_t close_btn_action = lv_btn_get_action(close_btn, LV_BTN_ACTION_CLICK);
		close_btn_action(close_btn);

		close_btn = NULL;

	 }
	// Button Joycon Function Close  Fix for Win to Win close built into FM main window with own custom close action
	if (jc_pad->b && close_firstwin)
	{
		lv_action_t close_btn_action = lv_btn_get_action(close_firstwin, LV_BTN_ACTION_CLICK);
		close_btn_action(close_firstwin);

		close_firstwin = NULL;

	}
	
	// Power Button Reload Menu Custom AtomNX function
	u8 btn = btn_read();

	if (btn & BTN_POWER)
	{
		reload_nyx();
	}

	return false; // No buffering so no more data read.
}

typedef struct _system_maintenance_tasks_t
{
	union
	{
		lv_task_t *tasks[2];
		struct
		{
			lv_task_t *status_bar;
			lv_task_t *dram_periodic_comp;
		} task;
	};
} system_maintenance_tasks_t;

static system_maintenance_tasks_t system_tasks;

void manual_system_maintenance(bool refresh)
{
	for (u32 task_idx = 0; task_idx < (sizeof(system_maintenance_tasks_t) / sizeof(lv_task_t *)); task_idx++)
	{
		lv_task_t *task = system_tasks.tasks[task_idx];
		if(task && (lv_tick_elaps(task->last_run) >= task->period))
		{
			task->last_run = lv_tick_get();
			task->task(task->param);
		}
	}
	if (refresh)
		lv_refr_now();
}

lv_img_dsc_t *bmp_to_lvimg_obj(const char *path)
{
	u32 fsize;
	u8 *bitmap = sd_file_read(path, &fsize);
	if (!bitmap)
		return NULL;

	struct _bmp_data
	{
		u32 size;
		u32 size_x;
		u32 size_y;
		u32 offset;
	};

	struct _bmp_data bmpData;

	// Get values manually to avoid unaligned access.
	bmpData.size = bitmap[2] | bitmap[3] << 8 |
		bitmap[4] << 16 | bitmap[5] << 24;
	bmpData.offset = bitmap[10] | bitmap[11] << 8 |
		bitmap[12] << 16 | bitmap[13] << 24;
	bmpData.size_x = bitmap[18] | bitmap[19] << 8 |
		bitmap[20] << 16 | bitmap[21] << 24;
	bmpData.size_y = bitmap[22] | bitmap[23] << 8 |
		bitmap[24] << 16 | bitmap[25] << 24;
	// Sanity check.
	if (bitmap[0] == 'B' &&
		bitmap[1] == 'M' &&
		bitmap[28] == 32 && // Only 32 bit BMPs allowed.
		bmpData.size <= fsize)
	{
		// Check if non-default Bottom-Top.
		bool flipped = false;
		if (bmpData.size_y & 0x80000000)
		{
			bmpData.size_y = ~(bmpData.size_y) + 1;
			flipped = true;
		}

		lv_img_dsc_t *img_desc = (lv_img_dsc_t *)bitmap;
		u32 offset_copy = ALIGN((u32)bitmap + sizeof(lv_img_dsc_t), 0x10);

		img_desc->header.always_zero = 0;
		img_desc->header.w = bmpData.size_x;
		img_desc->header.h = bmpData.size_y;
		img_desc->header.cf = (bitmap[28] == 32) ? LV_IMG_CF_TRUE_COLOR_ALPHA : LV_IMG_CF_TRUE_COLOR;
		img_desc->data_size = bmpData.size - bmpData.offset;
		img_desc->data = (u8 *)offset_copy;

		u32 *tmp = malloc(bmpData.size);
		u32 *tmp2 = (u32 *)offset_copy;

		// Copy the unaligned data to an aligned buffer.
		memcpy((u8 *)tmp, bitmap + bmpData.offset, img_desc->data_size);
		u32 j = 0;

		if (!flipped)
		{
			for (u32 y = 0; y < bmpData.size_y; y++)
			{
				for (u32 x = 0; x < bmpData.size_x; x++)
					tmp2[j++] = tmp[(bmpData.size_y - 1 - y ) * bmpData.size_x + x];
			}
		}
		else
		{
			for (u32 y = 0; y < bmpData.size_y; y++)
			{
				for (u32 x = 0; x < bmpData.size_x; x++)
					tmp2[j++] = tmp[y * bmpData.size_x + x];
			}
		}

		free(tmp);
	}
	else
	{
		free(bitmap);
		return NULL;
	}

	return (lv_img_dsc_t *)bitmap;
}

lv_res_t nyx_generic_onoff_toggle(lv_obj_t *btn)
{
	lv_obj_t *label_btn = lv_obj_get_child(btn, NULL);
	lv_obj_t *label_btn2 = lv_obj_get_child(btn, label_btn);

	char label_text[64];
	if (!label_btn2)
	{
		strcpy(label_text, lv_label_get_text(label_btn));
		label_text[strlen(label_text) - 15] = 0;

		if (!(lv_btn_get_state(btn) & LV_BTN_STATE_TGL_REL))
		{
			strcat(label_text, "#D0D0D0    OFF#");
			lv_label_set_text(label_btn, label_text);
		}
		else
		{
			s_printf(label_text, "%s%s%s", label_text, text_color, "    ON #");
			lv_label_set_text(label_btn, label_text);
		}
	}
	else
	{
		if (!(lv_btn_get_state(btn) & LV_BTN_STATE_TGL_REL))
			lv_label_set_text(label_btn, "#D0D0D0 OFF#");
		else
		{
			s_printf(label_text, "%s%s", text_color, " ON #");
			lv_label_set_text(label_btn, label_text);
		}
	}

	return LV_RES_OK;
}

lv_res_t mbox_action(lv_obj_t *btns, const char *txt)
{
	lv_obj_t *mbox = lv_mbox_get_from_btn(btns);
	lv_obj_t *dark_bg = lv_obj_get_parent(mbox);

	lv_obj_del(dark_bg); // Deletes children also (mbox).

	return LV_RES_INV;
}

bool nyx_emmc_check_battery_enough()
{
	if (fuse_read_hw_state() == FUSE_NX_HW_STATE_DEV)
		return true;

	int batt_volt = 0;

	max17050_get_property(MAX17050_VCELL, &batt_volt);

	if (batt_volt && batt_volt < 3650)
	{
		lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
		lv_obj_set_style(dark_bg, &mbox_darken);
		lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);

		static const char * mbox_btn_map[] = { "\211", "\222OK", "\211", "" };
		lv_obj_t * mbox = lv_mbox_create(dark_bg, NULL);
		lv_mbox_set_recolor_text(mbox, true);

		lv_mbox_set_text(mbox,
			"#FF8000 Battery Check#\n\n"
			"#FFDD00 Battery is not enough to carry on#\n"
			"#FFDD00 with selected operation!#\n\n"
			"Charge to at least #C7EA46 3650 mV#, and try again!");

		lv_mbox_add_btns(mbox, mbox_btn_map, mbox_action);
		lv_obj_set_width(mbox, LV_HOR_RES / 9 * 5);
		lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
		lv_obj_set_top(mbox, true);

		return false;
	}

	return true;
}

static void _nyx_sd_card_issues(void *param)
{
	lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
	lv_obj_set_style(dark_bg, &mbox_darken);
	lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);

	static const char * mbox_btn_map[] = { "\211", "\222OK", "\211", "" };
	lv_obj_t * mbox = lv_mbox_create(dark_bg, NULL);
	lv_mbox_set_recolor_text(mbox, true);

	lv_mbox_set_text(mbox,
		"#FF8000 SD Card Issues Check#\n\n"
		"#FFDD00 The SD Card is initialized in 1-bit mode!#\n"
		"#FFDD00 This might mean detached or broken connector!#\n\n"
		"You might want to check\n#C7EA46 Console Info# -> #C7EA46 microSD#");

	lv_mbox_add_btns(mbox, mbox_btn_map, mbox_action);
	lv_obj_set_width(mbox, LV_HOR_RES / 9 * 5);
	lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_top(mbox, true);
}

void nyx_window_toggle_buttons(lv_obj_t *win, bool disable)
{
	lv_win_ext_t * ext = lv_obj_get_ext_attr(win);
	lv_obj_t * hbtn;

	hbtn = lv_obj_get_child_back(ext->header, NULL);
	hbtn = lv_obj_get_child_back(ext->header, hbtn); // Skip the title.

	if (disable)
	{
		while (hbtn != NULL)
		{
			lv_obj_set_opa_scale(hbtn, LV_OPA_40);
			lv_obj_set_opa_scale_enable(hbtn, true);
			lv_obj_set_click(hbtn, false);
			hbtn = lv_obj_get_child_back(ext->header, hbtn);
		}
	}
	else
	{
		while (hbtn != NULL)
		{
			lv_obj_set_opa_scale(hbtn, LV_OPA_COVER);
			lv_obj_set_click(hbtn, true);
			hbtn = lv_obj_get_child_back(ext->header, hbtn);
		}
	}
}

lv_res_t lv_win_close_action_custom(lv_obj_t * btn)
{
    close_btn = NULL;

    return lv_win_close_action(btn);
}

lv_obj_t *nyx_create_standard_window(const char *win_title)
{
	static lv_style_t win_bg_style;

	lv_style_copy(&win_bg_style, &lv_style_plain);
	win_bg_style.body.main_color = lv_theme_get_current()->bg->body.main_color;
	win_bg_style.body.grad_color = win_bg_style.body.main_color;

	lv_obj_t *win = lv_win_create(lv_scr_act(), NULL);
	lv_win_set_title(win, win_title);
	lv_win_set_style(win, LV_WIN_STYLE_BG, &win_bg_style);
	lv_obj_set_size(win, LV_HOR_RES, LV_VER_RES);

	close_btn = lv_win_add_btn(win, NULL, SYMBOL_CLOSE" Close", lv_win_close_action_custom);

	return win;
}

lv_obj_t *nyx_create_window_custom_close_btn(const char *win_title, lv_action_t rel_action)
{
	static lv_style_t win_bg_style;

	lv_style_copy(&win_bg_style, &lv_style_plain);
	win_bg_style.body.main_color = lv_theme_get_current()->bg->body.main_color;
	win_bg_style.body.grad_color = win_bg_style.body.main_color;

	lv_obj_t *win = lv_win_create(lv_scr_act(), NULL);
	lv_win_set_title(win, win_title);
	lv_win_set_style(win, LV_WIN_STYLE_BG, &win_bg_style);
	lv_obj_set_size(win, LV_HOR_RES, LV_VER_RES);

	close_btn = lv_win_add_btn(win, NULL, SYMBOL_CLOSE" Close", rel_action);

	return win;
}
// Not Used From Nyx
/*static bool launch_logs_enable = false;

static void _launch_hos(u8 autoboot, u8 autoboot_list)
{
	b_cfg->boot_cfg = BOOT_CFG_AUTOBOOT_EN;
	if (launch_logs_enable)
		b_cfg->boot_cfg |= BOOT_CFG_FROM_LAUNCH;
	b_cfg->autoboot = autoboot;
	b_cfg->autoboot_list = autoboot_list;

	void (*main_ptr)() = (void *)nyx_str->hekate;

	sd_end();

	hw_reinit_workaround(false, 0);

	(*main_ptr)();
}*/

void reload_nyx()
{
	b_cfg->boot_cfg = BOOT_CFG_AUTOBOOT_EN;
	b_cfg->autoboot = 0;
	b_cfg->autoboot_list = 0;
	b_cfg->extra_cfg = 0;

	void (*main_ptr)() = (void *)nyx_str->hekate;

	sd_end();

	hw_reinit_workaround(false, 0);

	// Some cards (Sandisk U1), do not like a fast power cycle. Wait min 100ms.
	sdmmc_storage_init_wait_sd();

	(*main_ptr)();
}
// Restart AtomNX 
static lv_res_t reload_action(lv_obj_t *btns, const char *txt)
{
	if (!lv_btnm_get_pressed(btns))
		reload_nyx();

	return mbox_action(btns, txt);
}

// Reboot to RCM or OFW 
static lv_res_t reboot_rcm_ofw(lv_obj_t* btns, const char* txt)
{
	u32 btnidx = lv_btnm_get_pressed(btns);

	switch (btnidx)
	{
	case 0:
		power_set_state(REBOOT_BYPASS_FUSES);
		break;
	case 1:
		if (h_cfg.rcm_patched)
			power_set_state(POWER_OFF_REBOOT);
		else
			power_set_state(REBOOT_RCM);
		break;
	}

	return mbox_action(btns, txt);
}

static lv_res_t _removed_sd_action(lv_obj_t *btns, const char *txt)
{
	u32 btnidx = lv_btnm_get_pressed(btns);

	switch (btnidx)
	{
	case 0:
		if (h_cfg.rcm_patched)
			power_set_state(POWER_OFF_REBOOT);
		else
			power_set_state(REBOOT_RCM);
		break;
	case 1:
		power_set_state(POWER_OFF_RESET);
		break;
	case 2:
		sd_end();
		do_reload = false;
		break;
	}

	return mbox_action(btns, txt);
}

static void _check_sd_card_removed(void *params)
{
	// The following checks if SDMMC_1 is initialized.
	// If yes and card was removed, shows a message box,
	// that will reload Nyx, when the card is inserted again.
	if (!do_reload && sd_get_card_removed())
	{
		lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
		lv_obj_set_style(dark_bg, &mbox_darken);
		lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);

		static const char * mbox_btn_map[] = { "\221Reboot (RCM)", "\221Power Off", "\221Do not reload", "" };
		static const char * mbox_btn_map_rcm_patched[] = { "\221Reboot", "\221Power Off", "\221Do not reload", "" };
		lv_obj_t *mbox = lv_mbox_create(dark_bg, NULL);
		lv_mbox_set_recolor_text(mbox, true);
		lv_obj_set_width(mbox, LV_HOR_RES * 6 / 9);

		lv_mbox_set_text(mbox, "\n#FF8000 SD card was removed!#\n\n#96FF00 Nyx will reload after inserting it.#\n");
		lv_mbox_add_btns(mbox, h_cfg.rcm_patched ? mbox_btn_map_rcm_patched : mbox_btn_map, _removed_sd_action);

		lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
		lv_obj_set_top(mbox, true);

		do_reload = true;
	}

	// If in reload state and card was inserted, reload nyx.
	if (do_reload && !sd_get_card_removed())
		reload_nyx();
}

lv_task_t *task_emmc_errors;
static void _nyx_emmc_issues(void *params)
{
	if (emmc_get_mode() < EMMC_MMC_HS400)
	{
		// Remove task.
		lv_task_del(task_emmc_errors);

		lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
		lv_obj_set_style(dark_bg, &mbox_darken);
		lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);

		static const char * mbox_btn_map[] = { "\211", "\222OK", "\211", "" };
		lv_obj_t * mbox = lv_mbox_create(dark_bg, NULL);
		lv_mbox_set_recolor_text(mbox, true);

		lv_mbox_set_text(mbox,
			"#FF8000 eMMC Issues Check#\n\n"
			"#FFDD00 Your eMMC is initialized in slower mode!#\n"
			"#FFDD00 This might mean hardware issues!#\n\n"
			"You might want to check\n#C7EA46 Console Info# -> #C7EA46 eMMC#");

		lv_mbox_add_btns(mbox, mbox_btn_map, mbox_action);
		lv_obj_set_width(mbox, LV_HOR_RES / 9 * 5);
		lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
		lv_obj_set_top(mbox, true);
	}
}

void nyx_create_onoff_button(lv_theme_t *th, lv_obj_t *parent, lv_obj_t *btn, const char *btn_name, lv_action_t action, bool transparent)
{
	// Create buttons that are flat and text, plus On/Off switch.
	static lv_style_t btn_onoff_rel_hos_style, btn_onoff_pr_hos_style;
	lv_style_copy(&btn_onoff_rel_hos_style, th->btn.rel);
	btn_onoff_rel_hos_style.body.shadow.width = 0;
	btn_onoff_rel_hos_style.body.border.width = 0;
	btn_onoff_rel_hos_style.body.padding.hor = 0;
	btn_onoff_rel_hos_style.body.radius = 0;
	btn_onoff_rel_hos_style.body.empty = 1;

	lv_style_copy(&btn_onoff_pr_hos_style, &btn_onoff_rel_hos_style);
	if (transparent)
	{
		btn_onoff_pr_hos_style.body.main_color = LV_COLOR_HEX(0xFFFFFF);
		btn_onoff_pr_hos_style.body.opa = 35;
	}
	else
		btn_onoff_pr_hos_style.body.main_color = LV_COLOR_HEX(0x3D3D3D);
	btn_onoff_pr_hos_style.body.grad_color = btn_onoff_pr_hos_style.body.main_color;
	btn_onoff_pr_hos_style.text.color = th->btn.pr->text.color;
	btn_onoff_pr_hos_style.body.empty = 0;

	lv_obj_t *label_btn = lv_label_create(btn, NULL);
	lv_obj_t *label_btnsw = NULL;

	lv_label_set_recolor(label_btn, true);
	label_btnsw = lv_label_create(btn, NULL);
	lv_label_set_recolor(label_btnsw, true);
	lv_btn_set_layout(btn, LV_LAYOUT_OFF);

	lv_btn_set_style(btn, LV_BTN_STYLE_REL, &btn_onoff_rel_hos_style);
	lv_btn_set_style(btn, LV_BTN_STYLE_PR, &btn_onoff_pr_hos_style);
	lv_btn_set_style(btn, LV_BTN_STYLE_TGL_REL, &btn_onoff_rel_hos_style);
	lv_btn_set_style(btn, LV_BTN_STYLE_TGL_PR, &btn_onoff_pr_hos_style);

	lv_btn_set_fit(btn, false, true);
	lv_obj_set_width(btn, lv_obj_get_width(parent));
	lv_btn_set_toggle(btn, true);

	lv_label_set_text(label_btn, btn_name);

	lv_label_set_text(label_btnsw, "#D0D0D0 OFF#");
	lv_obj_align(label_btn, btn, LV_ALIGN_IN_LEFT_MID, LV_DPI / 4, 0);
	lv_obj_align(label_btnsw, btn, LV_ALIGN_IN_RIGHT_MID, -LV_DPI / 4, -LV_DPI / 10);

	if (action)
		lv_btn_set_action(btn, LV_BTN_ACTION_CLICK, action);
}

#include <libs/lvgl/lv_themes/lv_theme_cwad.h>


const char* infotext = "";

// AtomNX Theme Starts Here                    
static void load_default_styles(lv_theme_t *th)
{
	// Background style outside of MBOX
	lv_style_copy(&mbox_darken, &lv_style_plain);
	mbox_darken.body.main_color = LV_COLOR_BLACK;
	mbox_darken.body.grad_color = mbox_darken.body.main_color;
	mbox_darken.body.opa = LV_OPA_50;
	mbox_darken.body.radius = 5;

	lv_style_copy(&hint_small_style, th->label.hint);
	hint_small_style.text.letter_space = 1;
	hint_small_style.text.font = &interui_20;

	lv_style_copy(&hint_small_style_white, th->label.prim);
	hint_small_style_white.text.letter_space = 1;
	hint_small_style_white.text.font = &interui_20;

	lv_style_copy(&monospace_text, &lv_style_plain);
	monospace_text.body.main_color = LV_COLOR_HEX(0x1B1B1B);
	monospace_text.body.grad_color = LV_COLOR_HEX(0x1B1B1B);
	monospace_text.body.border.color = LV_COLOR_HEX(0x1B1B1B);
	monospace_text.body.border.width = 0;
	monospace_text.body.opa = LV_OPA_TRANSP;
	monospace_text.text.color = LV_COLOR_HEX(0xD8D8D8);
	monospace_text.text.font = &ubuntu_mono;
	monospace_text.text.letter_space = 0;
	monospace_text.text.line_space = 0;

	// PL Button and LabelBtn Style rel
	lv_style_copy(&btn_transp_rel, th->btn.rel);
	btn_transp_rel.body.main_color = LV_COLOR_HEX(0x444444);
	btn_transp_rel.body.grad_color = btn_transp_rel.body.main_color;
	btn_transp_rel.body.opa = LV_OPA_50;

	// PL Button and LabelBtn Style pr
	lv_style_copy(&btn_transp_pr, th->btn.pr);
	btn_transp_pr.body.main_color = LV_COLOR_HEX(0x888888);
	btn_transp_pr.body.grad_color = btn_transp_pr.body.main_color;
	btn_transp_pr.body.opa = LV_OPA_50;

	// Tabview Buttons
	lv_style_copy(&tabview_btn_pr, th->tabview.btn.pr);
	tabview_btn_pr.body.main_color = LV_COLOR_HEX(0xFFFFFF);
	tabview_btn_pr.body.grad_color = tabview_btn_pr.body.main_color;
	tabview_btn_pr.body.opa = 35;

	// Tabview Buttons
	lv_style_copy(&tabview_btn_tgl_pr, th->tabview.btn.tgl_pr);
	tabview_btn_tgl_pr.body.main_color = LV_COLOR_HEX(0xFFFFFF);
	tabview_btn_tgl_pr.body.grad_color = tabview_btn_tgl_pr.body.main_color;
	tabview_btn_tgl_pr.body.opa = 35;

	// Header Style configuration
	lv_style_copy(&header_style, &lv_style_pretty);
	header_style.text.color = LV_COLOR_WHITE;
	header_style.text.font = &interui_30;
	header_style.body.opa = LV_OPA_50;

	// Window Background Style
	lv_style_copy(&win_bg_style, &lv_style_plain);
	win_bg_style.body.padding.left = LV_DPI / 6;
	win_bg_style.body.padding.right = LV_DPI / 6;
	win_bg_style.body.padding.top = 0;
	win_bg_style.body.padding.bottom = 0;
	win_bg_style.body.padding.inner = LV_DPI / 6;
	win_bg_style.body.main_color = lv_theme_get_current()->bg->body.main_color;
	win_bg_style.body.grad_color = win_bg_style.body.main_color;
	win_bg_style.body.opa = LV_OPA_80;

	// Style for keyboard
	lv_style_copy(&style_kb_rel, &lv_style_plain);
	style_kb_rel.body.opa = LV_OPA_TRANSP;
	style_kb_rel.body.radius = 0;
	style_kb_rel.body.border.width = 1;
	style_kb_rel.body.border.color = LV_COLOR_SILVER;
	style_kb_rel.body.border.opa = LV_OPA_50;
	style_kb_rel.body.main_color = LV_COLOR_HEX3(0x333);
	style_kb_rel.body.grad_color = LV_COLOR_HEX3(0x333);
	style_kb_rel.text.color = LV_COLOR_WHITE;

	// Style for keyboard
	lv_style_copy(&style_kb_pr, &lv_style_plain);
	style_kb_pr.body.radius = 0;
	style_kb_pr.body.opa = LV_OPA_50;
	style_kb_pr.body.main_color = LV_COLOR_WHITE;
	style_kb_pr.body.grad_color = LV_COLOR_WHITE;
	style_kb_pr.body.border.width = 1;
	style_kb_pr.body.border.color = LV_COLOR_SILVER;

	// Definition Font Size 20
	lv_style_copy(&font20_style, &lv_style_plain);
	font20_style.text.color = LV_COLOR_WHITE;
	font20_style.text.font = &interui_20;

	// Definition font size 20 red
	lv_style_copy(&font20red_style, &lv_style_plain);
	font20red_style.text.color = LV_COLOR_RED;
	font20red_style.text.font = &interui_20;

	// Definition font size 20 green
	lv_style_copy(&font20green_style, &lv_style_plain);
	font20green_style.text.color = LV_COLOR_GREEN;
	font20green_style.text.font = &interui_20;

	// Definition label font 30
	lv_style_copy(&labels_style, lv_theme_get_current()->label.prim);
	labels_style.text.color = LV_COLOR_WHITE;

	// Definition transparent label
	lv_style_copy(&inv_label, &lv_style_transp);
	inv_label.text.font = NULL;
}

// Default Window win Style e.g. for HW Infopage win
lv_obj_t *gui_create_standard_window(const char *win_title)
{
	lv_obj_t *win = lv_win_create(lv_scr_act(), NULL);
	lv_win_set_title(win, win_title);
	lv_win_set_style(win, LV_WIN_STYLE_BG, &win_bg_style);
	lv_win_set_style(win, LV_WIN_STYLE_HEADER, &header_style);
	lv_obj_set_size(win, LV_HOR_RES, LV_VER_RES);
	lv_win_set_btn_size(win, 45);

	close_btn = lv_win_add_btn(win, NULL, SYMBOL_CLOSE" Close", lv_win_close_action_custom);

	return win;
}

// Custom close action for first window
lv_res_t lv_win_close_action_firstwin(lv_obj_t *btn)
{

	close_firstwin = NULL;// close_btn = NULL;

	return lv_win_close_action(btn);
}

// Restart AtomNX Confirm Dialog																											
static lv_res_t _create_mbox_reload(lv_obj_t *btn)
{
	// MBOX Background Style definition
	static lv_style_t mbox_bg;
	lv_style_copy(&mbox_bg, &lv_style_pretty);
	mbox_bg.body.main_color = LV_COLOR_BLACK;
	mbox_bg.body.grad_color = mbox_darken.body.main_color;
	mbox_bg.body.opa = LV_OPA_40;

	// Background behind MBOX
	lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
	lv_obj_set_style(dark_bg, &mbox_darken);
	lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);

	static const char *mbox_btn_map[] = { "\221#55d41E Confirm", "\221Cancel", "" };
	lv_obj_t *mbox = lv_mbox_create(dark_bg, NULL);
	lv_mbox_set_recolor_text(mbox, true);
	lv_obj_set_width(mbox, LV_HOR_RES * 4 / 10);
	lv_mbox_set_text(mbox, "#E0190A Attention!! #\n#FF8000 You are about to reload the application.#");
	lv_mbox_add_btns(mbox, mbox_btn_map, reload_action);

	// Buttonmap Style MBOX
	lv_mbox_set_style(mbox, LV_MBOX_STYLE_BTN_BG, &lv_style_transp);// MBOX Buttons style background
	lv_mbox_set_style(mbox, LV_MBOX_STYLE_BTN_REL, &btn_transp_rel);// MBOX Buttons style Release
	lv_mbox_set_style(mbox, LV_MBOX_STYLE_BTN_PR, &btn_transp_pr);// MBOX Buttons style Pressed

	lv_mbox_set_style(mbox, LV_MBOX_STYLE_BG, &mbox_bg);// MBOX Background Style carry out


	lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_top(mbox, true);

	return LV_RES_OK;
}

// Reboot To AtomNX or OFW Selection
static lv_res_t _create_rcm_ofw_reboot(lv_obj_t *btn)
{
	// MBOX Background Style definition
	static lv_style_t mbox_bg;
	lv_style_copy(&mbox_bg, &lv_style_pretty);
	mbox_bg.body.main_color = LV_COLOR_BLACK;
	mbox_bg.body.grad_color = mbox_darken.body.main_color;
	mbox_bg.body.opa = LV_OPA_40;

	// Background behind MBOX
	lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
	lv_obj_set_style(dark_bg, &mbox_darken);
	lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);

	static const char *mbox_btn_map[] = { "\221OFW", "\221RCM", "\221Cancel", "" };
	static const char *mbox_btn_map_patched[] = { "\221OFW", "\221Normal", "\221Cancel", "" };
	lv_obj_t *mbox = lv_mbox_create(dark_bg, NULL);
	lv_mbox_set_recolor_text(mbox, true);
	lv_obj_set_width(mbox, LV_HOR_RES * 4 / 10);
	lv_mbox_set_text(mbox, "#FF8000 Choose where to reboot!#");
	lv_mbox_add_btns(mbox, h_cfg.rcm_patched ? mbox_btn_map_patched : mbox_btn_map, reboot_rcm_ofw);

	// Buttonmap Style MBOX
	lv_mbox_set_style(mbox, LV_MBOX_STYLE_BTN_BG, &lv_style_transp);// MBOX Buttons style background
	lv_mbox_set_style(mbox, LV_MBOX_STYLE_BTN_REL, &btn_transp_rel);// MBOX Buttons style Release
	lv_mbox_set_style(mbox, LV_MBOX_STYLE_BTN_PR, &btn_transp_pr);// MBOX Buttons style Pressed

	lv_mbox_set_style(mbox, LV_MBOX_STYLE_BG, &mbox_bg);// MBOX Background Style carry out


	lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_top(mbox, true);

	return LV_RES_OK;
}

// Function Power off
static lv_res_t poweroff(lv_obj_t* btn)
{
	power_set_state(POWER_OFF_RESET);
	return LV_RES_OK;
}

// Info Button Function															*************************** FIX AUTOCLOSE DUPLICATES
static lv_res_t ctrl_info(lv_obj_t *btn)
{
	static lv_style_t bg;
	lv_style_copy(&bg, &lv_style_pretty);
	bg.text.color = LV_COLOR_WHITE;
	bg.body.opa = LV_OPA_0;
	bg.text.font = &atomfont;

	lv_obj_t *mbox = lv_mbox_create(lv_layer_top(), NULL);
	lv_mbox_set_recolor(mbox, true);
	lv_obj_set_width(mbox, LV_DPI * 5);
	lv_obj_set_top(mbox, true);
	lv_obj_set_auto_realign(mbox, true);
	lv_obj_align(mbox, NULL, LV_ALIGN_IN_BOTTOM_MID, 0, 10);
	lv_mbox_set_text(mbox, "AtomNX v0.05" SYMBOL_ATOM "Custom Bootloader Redux\nGui by CantWeAllDisagree\nHekate BDK & Libs v5.8.0" SYMBOL_PEACE);
	lv_mbox_set_style(mbox, LV_MBOX_STYLE_BG, &bg);
	lv_mbox_start_auto_close(mbox, 6420);

	return LV_RES_OK;
}

// Definition static keyboard
static lv_obj_t *kb;

// Definition static text fields
static lv_obj_t *perhr;
static lv_obj_t *permin;
static lv_obj_t *perday;
static lv_obj_t *permonth;
static lv_obj_t *peryear;

// Window change at TA
static lv_res_t ta_event_action(lv_obj_t *ta)
{
	lv_ta_set_cursor_type(ta, LV_CURSOR_HIDDEN);
	lv_ta_set_cursor_type(ta, LV_CURSOR_BLOCK);
	lv_kb_set_ta(kb, ta);

	return LV_RES_OK;
}

// Button Function Set time and date RTC SAVE
static lv_res_t ctrl_rtctimesave(lv_obj_t *btn)
{
	// RTC time fields definition...
	rtc_time_t time;

	const char *gethr    = lv_ta_get_text(perhr);
	const char *getmin   = lv_ta_get_text(permin);
	const char *getday   = lv_ta_get_text(perday);
	const char *getmonth = lv_ta_get_text(permonth);
	const char *getyear  = lv_ta_get_text(peryear);

	// Convert numbers to int, string to int: atoi
	int hours = atoi(gethr);
	int min   = atoi(getmin);
	int day   = atoi(getday);
	int month = atoi(getmonth);
	int year  = atoi(getyear);

	// Insert and correct int in time fields
	time.hour  = hours;
	time.min   = min;
	time.day   = day;
	time.month = month;
	time.year  = year;

	// Time zone CET+1 Undo                             *************************** Figure out how to change to EST time
	if (time.hour == 00) time.hour = 23;
	else time.hour -= 1;

	// Daylight Saving Time Undo
	int MEZ = 0;

	if (time.month > 3 && time.month < 10)// DST in Apr, May, Jun, Jul, Aug, Sep
	{
		if (time.hour == 00) time.hour = 23;
		else time.hour -= 1;
	}

	if (time.month == 3 && (time.hour + 24 * time.day) >= (1 + MEZ + 24 * (31 - (5 * time.year / 4 + 4) % 7)))
	{
		if (time.hour == 00) time.hour = 23;
		else time.hour -= 1;
	}

	if (time.month == 10 && (time.hour + 24 * time.day) < (1 + MEZ + 24 * (31 - (5 * time.year / 4 + 1) % 7)))
	{
		if (time.hour == 00) time.hour = 23;
		else time.hour -= 1;
	}

	time.year -= 2000;//year prefix 20 save to away

	// Set RTC Time
	i2c_send_byte(I2C_5, MAX77620_RTC_I2C_ADDR, MAX77620_RTC_HOUR_REG, time.hour);
	i2c_send_byte(I2C_5, MAX77620_RTC_I2C_ADDR, MAX77620_RTC_MIN_REG, time.min);
	i2c_send_byte(I2C_5, MAX77620_RTC_I2C_ADDR, MAX77620_RTC_DATE_REG, time.day);
	i2c_send_byte(I2C_5, MAX77620_RTC_I2C_ADDR, MAX77620_RTC_MONTH_REG, time.month);
	i2c_send_byte(I2C_5, MAX77620_RTC_I2C_ADDR, MAX77620_RTC_YEAR_REG, time.year);

	// Update RTC clock from RTC regs.
	i2c_send_byte(I2C_5, MAX77620_RTC_I2C_ADDR, MAX77620_RTC_UPDATE0_REG, MAX77620_RTC_WRITE_UPDATE);

	// Definition Textbox Style
	static lv_style_t bg;
	lv_style_copy(&bg, &lv_style_pretty);
	bg.text.color = LV_COLOR_WHITE;
	bg.body.opa = LV_OPA_0;
	bg.text.font = &interui_20;

	lv_obj_t *mbox = lv_mbox_create(lv_layer_top(), NULL);
	lv_mbox_set_recolor(mbox, true);
	lv_obj_set_width(mbox, LV_DPI * 5);
	lv_obj_set_top(mbox, true);
	lv_obj_set_auto_realign(mbox, true);
	lv_obj_align(mbox, NULL, LV_ALIGN_IN_TOP_MID, 0, 5);
	lv_mbox_set_text(mbox, "RTC Time and Date saved!");
	lv_mbox_set_style(mbox, LV_MBOX_STYLE_BG, &bg);
	lv_mbox_start_auto_close(mbox, 8000);


	return LV_RES_OK;
}

// Button Function Set time and date RTC
static lv_res_t ctrl_rtctime(lv_obj_t *btn)
{
	// Text window style configuration font 110px
	static lv_style_t tafont110_style;
	lv_style_copy(&tafont110_style, &lv_style_pretty);
	tafont110_style.text.color = LV_COLOR_WHITE;
	tafont110_style.text.font = &num_110;
	tafont110_style.body.opa = LV_OPA_20;

	// Create a window to hold all the objects
	lv_obj_t *win = lv_win_create(lv_scr_act(), NULL);
	lv_win_set_title(win, "RTC Time and Date");
	lv_page_set_scrl_layout(lv_win_get_content(win), LV_LAYOUT_OFF);
	lv_win_set_style(win, LV_WIN_STYLE_HEADER, &header_style);
	lv_win_set_style(win, LV_WIN_STYLE_BG, &win_bg_style);

	// Add control button to the header also OK
	close_btn = lv_win_add_btn(win, NULL, SYMBOL_CLOSE, lv_win_close_action_custom);
	lv_obj_set_style(close_btn, LV_LABEL_STYLE_MAIN);

	lv_obj_t *save_btn = lv_win_add_btn(win, NULL, SYMBOL_SAVE, ctrl_rtctimesave);
	lv_obj_set_style(save_btn, LV_LABEL_STYLE_MAIN);

	lv_win_set_btn_size(win, 45);

	//Read RTC...
	rtc_time_t time;

	// Get sensor data Clock
	u8 val = 0;

	// Update RTC regs from RTC clock.
	i2c_send_byte(I2C_5, MAX77620_RTC_I2C_ADDR, MAX77620_RTC_UPDATE0_REG, MAX77620_RTC_READ_UPDATE);

	// Get control reg config.
	val = i2c_recv_byte(I2C_5, MAX77620_RTC_I2C_ADDR, MAX77620_RTC_CONTROL_REG);

	// Get time.
	time.sec = i2c_recv_byte(I2C_5, MAX77620_RTC_I2C_ADDR, MAX77620_RTC_SEC_REG) & 0x7F;
	time.min = i2c_recv_byte(I2C_5, MAX77620_RTC_I2C_ADDR, MAX77620_RTC_MIN_REG) & 0x7F;

	time.hour = i2c_recv_byte(I2C_5, MAX77620_RTC_I2C_ADDR, MAX77620_RTC_HOUR_REG) & 0x1F;

	if (!(val & MAX77620_RTC_24H) && time.hour & MAX77620_RTC_HOUR_PM_MASK)
		time.hour = (time.hour & 0xF) + 12;

	// Get day of week. 1: Monday to 7: Sunday.
	time.weekday = 0;
	val = i2c_recv_byte(I2C_5, MAX77620_RTC_I2C_ADDR, MAX77620_RTC_WEEKDAY_REG);
	for (int i = 0; i < 8; i++)
	{
		time.weekday++;
		if (val & 1)
			break;
		val >>= 1;
	}

	// Get date.
	time.day = i2c_recv_byte(I2C_5, MAX77620_RTC_I2C_ADDR, MAX77620_RTC_DATE_REG) & 0x1f;
	time.year = (i2c_recv_byte(I2C_5, MAX77620_RTC_I2C_ADDR, MAX77620_RTC_YEAR_REG) & 0x7F) + 2000;
	time.month = i2c_recv_byte(I2C_5, MAX77620_RTC_I2C_ADDR, MAX77620_RTC_MONTH_REG) & 0xF;

	// Time zone CET+1
	if (time.hour == 23) time.hour = 0;
	else time.hour += 1;

	// time.month = (time.month) - 8;//Test Summertime

	// Summertime
	int MEZ = 0;

	if (time.month > 3 && time.month < 10)// Summertime in Apr, Mai, Jun, Jul, Aug, Sep
	{
		if (time.hour == 23) time.hour = 0;
		else time.hour += 1;
	}

	// Retest
	if (time.month == 3 && (time.hour + 24 * time.day) >= (1 + MEZ + 24 * (31 - (5 * time.year / 4 + 4) % 7)))
	{
		if (time.hour == 23) time.hour = 0;
		else time.hour += 1;
	}

	if (time.month == 10 && (time.hour + 24 * time.day) < (1 + MEZ + 24 * (31 - (5 * time.year / 4 + 1) % 7)))
	{
		if (time.hour == 23) time.hour = 0;
		else time.hour += 1;
	}

	char *hr = (char*)malloc(64);
	char *min = (char*)malloc(64);
	char *day = (char*)malloc(64);
	char *month = (char*)malloc(64);
	char *year = (char*)malloc(64);

	s_printf(hr, "%02d", time.hour);
	s_printf(min, "%02d", time.min);
	s_printf(day, "%02d", time.day);
	s_printf(month, "%02d", time.month);
	s_printf(year, "%02d", time.year);

	// Define and display text window, main definition above outside function static
	perhr = lv_ta_create(win, NULL);
	lv_obj_set_size(perhr, 190, 140);
	lv_obj_set_pos(perhr, 25, 50);
	lv_ta_set_text(perhr, hr);
	lv_ta_set_max_length(perhr, 2);
	lv_ta_set_style(perhr, LV_LABEL_STYLE_MAIN, &tafont110_style);
	lv_ta_set_cursor_type(perhr, LV_CURSOR_BLOCK);
	lv_ta_set_action(perhr, ta_event_action);

	// Label date and time text box
	lv_obj_t *label = lv_label_create(win, NULL);
	lv_label_set_text(label, "Hour");
	lv_obj_align(label, perhr, LV_ALIGN_OUT_BOTTOM_MID, 0, 30);
	lv_label_set_style(label, &header_style);

	permin = lv_ta_create(win, NULL);
	lv_ta_set_cursor_type(permin, LV_CURSOR_BLOCK | LV_CURSOR_HIDDEN);
	lv_obj_set_size(permin, 190, 140);
	lv_obj_set_pos(permin, 235, 16);
	lv_ta_set_text(permin, min);
	lv_ta_set_max_length(permin, 2);
	lv_ta_set_style(permin, LV_LABEL_STYLE_MAIN, &tafont110_style);
	lv_ta_set_action(permin, ta_event_action);

	// Label date and time text box
	label = lv_label_create(win, NULL);
	lv_label_set_text(label, "Minute");
	lv_obj_align(label, permin, LV_ALIGN_OUT_BOTTOM_MID, 0, 30);
	lv_label_set_style(label, &header_style);

	perday = lv_ta_create(win, NULL);
	lv_ta_set_cursor_type(perday, LV_CURSOR_BLOCK | LV_CURSOR_HIDDEN);
	lv_obj_set_size(perday, 190, 140);
	lv_obj_set_pos(perday, 495, 16);
	lv_ta_set_text(perday, day);
	lv_ta_set_max_length(perday, 2);
	lv_ta_set_style(perday, LV_LABEL_STYLE_MAIN, &tafont110_style);
	lv_ta_set_action(perday, ta_event_action);

	// Label date and time text box
	label = lv_label_create(win, NULL);
	lv_label_set_text(label, "Day");
	lv_obj_align(label, perday, LV_ALIGN_OUT_BOTTOM_MID, 0, 30);
	lv_label_set_style(label, &header_style);

	permonth = lv_ta_create(win, NULL);
	lv_ta_set_cursor_type(permonth, LV_CURSOR_BLOCK | LV_CURSOR_HIDDEN);
	lv_obj_set_size(permonth, 190, 140);
	lv_obj_set_pos(permonth, 705, 16);
	lv_ta_set_text(permonth, month);
	lv_ta_set_max_length(permonth, 2);
	lv_ta_set_style(permonth, LV_LABEL_STYLE_MAIN, &tafont110_style);
	lv_ta_set_action(permonth, ta_event_action);

	// Label date and time text box
	label = lv_label_create(win, NULL);
	lv_label_set_text(label, "Month");
	lv_obj_align(label, permonth, LV_ALIGN_OUT_BOTTOM_MID, 0, 30);
	lv_label_set_style(label, &header_style);

	peryear = lv_ta_create(win, NULL);
	lv_ta_set_cursor_type(peryear, LV_CURSOR_BLOCK | LV_CURSOR_HIDDEN);
	lv_obj_set_size(peryear, 330, 140);
	lv_obj_set_pos(peryear, 915, 16);
	lv_ta_set_text(peryear, year);
	lv_ta_set_max_length(peryear, 4);
	lv_ta_set_style(peryear, LV_LABEL_STYLE_MAIN, &tafont110_style);
	lv_ta_set_action(peryear, ta_event_action);

	// Label date and time text box
	label = lv_label_create(win, NULL);
	lv_label_set_text(label, "Year");
	lv_obj_align(label, peryear, LV_ALIGN_OUT_BOTTOM_MID, 0, 30);
	lv_label_set_style(label, &header_style);

	// Label date and time text box
	lv_obj_t *label_points = lv_label_create(win, NULL);
	lv_label_set_text(label_points, ":");
	lv_obj_set_pos(label_points, 220, 16);
	lv_label_set_style(label_points, &tafont110_style);

	lv_obj_t *label_points1 = lv_label_create(win, NULL);
	lv_label_set_text(label_points1, ".");
	lv_obj_set_pos(label_points1, 690, 16);
	lv_label_set_style(label_points1, &tafont110_style);

	lv_obj_t *label_points2 = lv_label_create(win, NULL);
	lv_label_set_text(label_points2, ".");
	lv_obj_set_pos(label_points2, 900, 16);
	lv_label_set_style(label_points2, &tafont110_style);

	free(hr);
	free(min);
	free(day);
	free(month);
	free(year);

	// Label date and time text box							
	kb = lv_kb_create(win, NULL);
	lv_obj_set_size(kb, 1080, 300);
	lv_obj_set_pos(kb, 100, 266);
	lv_kb_set_mode(kb, LV_KB_MODE_NUM);
	lv_kb_set_ta(kb, perhr);
	lv_kb_set_cursor_manage(kb, true);

	// Label date and time text box
	lv_kb_set_style(kb, LV_KB_STYLE_BTN_REL, &style_kb_rel);
	lv_kb_set_style(kb, LV_KB_STYLE_BTN_PR, &style_kb_pr);

	return LV_RES_OK;
}

// Tasks refresh definition
static void update_status(void *params)
{
	// Read and display date and time
	char *times = (char*)malloc(64);

	rtc_time_t time;

	// Get sensor data Clock
	u8 val = 0;

	// Update RTC regs from RTC clock.
	i2c_send_byte(I2C_5, MAX77620_RTC_I2C_ADDR, MAX77620_RTC_UPDATE0_REG, MAX77620_RTC_READ_UPDATE);

	// Get control reg config.
	val = i2c_recv_byte(I2C_5, MAX77620_RTC_I2C_ADDR, MAX77620_RTC_CONTROL_REG);

	// Get time.
	time.sec = i2c_recv_byte(I2C_5, MAX77620_RTC_I2C_ADDR, MAX77620_RTC_SEC_REG) & 0x7F;
	time.min = i2c_recv_byte(I2C_5, MAX77620_RTC_I2C_ADDR, MAX77620_RTC_MIN_REG) & 0x7F;

	time.hour = i2c_recv_byte(I2C_5, MAX77620_RTC_I2C_ADDR, MAX77620_RTC_HOUR_REG) & 0x1F;

	if (!(val & MAX77620_RTC_24H) && time.hour & MAX77620_RTC_HOUR_PM_MASK)
		time.hour = (time.hour & 0xF) + 12;

	// Get day of week. 1: Monday to 7: Sunday.
	time.weekday = 0;
	val = i2c_recv_byte(I2C_5, MAX77620_RTC_I2C_ADDR, MAX77620_RTC_WEEKDAY_REG);
	for (int i = 0; i < 8; i++)
	{
		time.weekday++;
		if (val & 1)
			break;
		val >>= 1;
	}

	// Get date.
	time.day = i2c_recv_byte(I2C_5, MAX77620_RTC_I2C_ADDR, MAX77620_RTC_DATE_REG) & 0x1f;
	time.year = (i2c_recv_byte(I2C_5, MAX77620_RTC_I2C_ADDR, MAX77620_RTC_YEAR_REG) & 0x7F) + 2000;
	time.month = i2c_recv_byte(I2C_5, MAX77620_RTC_I2C_ADDR, MAX77620_RTC_MONTH_REG) & 0xF;

	// Time zone CET+1
	if (time.hour == 23) time.hour = 0;
	else time.hour += 1;

	// Summertime
	int MEZ = 0;

	if (time.month > 3 && time.month < 10)// Summertime in Apr, Mai, Jun, Jul, Aug, Sep
	{
		if (time.hour == 23) time.hour = 0;
		else time.hour += 1;
	}

	// Retest
	if (time.month == 3 && (time.hour + 24 * time.day) >= (1 + MEZ + 24 * (31 - (5 * time.year / 4 + 4) % 7)))
	{
		if (time.hour == 23) time.hour = 0;
		else time.hour += 1;
	}

	if (time.month == 10 && (time.hour + 24 * time.day) < (1 + MEZ + 24 * (31 - (5 * time.year / 4 + 1) % 7)))
	{
		if (time.hour == 23) time.hour = 0;
		else time.hour += 1;
	}


	// Set time and Date                                                                   
	s_printf(times, "%02d.%02d.%02d "" %02d:%02d:%02d",
		time.day, time.month, time.year, time.hour, time.min, time.sec);

	lv_label_set_array_text(status_bar.time_date, times, 64);

	free(times);																						//******************************************************************** END OF RTC *******


	// Read out the battery and display the symbol depending on the level
	u32 battPercent = 0;
	max17050_get_property(MAX17050_RepSOC, (int*)&battPercent);

	int per1 = (battPercent >> 8) & 0xFF;
	int per2 = (battPercent & 0xFF) / 25.5001; //exact to 4 decimal places
	if (per2 >= 0)
		per1 = per1 + 1; // keep value the same as the switch main screen

	if (per1 >= 101)// Correction battery 101%
		per1 = per1 - 1;


	// Battery Icon
	if (per1 > 0) {
		lv_label_set_array_text(status_bar.batterysym, SYMBOL_BATTERY_EMPTY, 64);
	}

	if (per1 > 17) {
		lv_label_set_array_text(status_bar.batterysym, SYMBOL_BATTERY_1, 64);
	}

	if (per1 > 34) {
		lv_label_set_array_text(status_bar.batterysym, SYMBOL_BATTERY_1, 64);
	}

	if (per1 > 51) {
		lv_label_set_array_text(status_bar.batterysym, SYMBOL_BATTERY_2, 64);
	}

	if (per1 > 68) {
		lv_label_set_array_text(status_bar.batterysym, SYMBOL_BATTERY_3, 64);
	}

	if (per1 > 84) {
		lv_label_set_array_text(status_bar.batterysym, SYMBOL_BATTERY_FULL, 64);
	}

	if (per1 <= 5) {
		lv_label_set_array_text(status_bar.batterysym, SYMBOL_BATTERY_EMPTY"\nWarning, battery almost empty! Please connect charger!", 64);
	}

	// Show info text battery percent
	if (per1 < 20) {

		char *battery = (char*)malloc(0x1000);
		s_printf(battery, "%d %%", per1);

		lv_label_set_array_text(status_bar.charging, battery, 64);
		lv_label_set_style(status_bar.charging, &font20red_style);

		free(battery);

	}

	else {

		char *battery = (char*)malloc(0x1000);
		s_printf(battery, "%d %%", per1);

		lv_label_set_array_text(status_bar.charging, battery, 64);
		lv_label_set_style(status_bar.charging, &font20_style);

		free(battery);

	}

	// Power consumption and volt display
	char *amp = (char*)malloc(64);
	char *volt = (char*)malloc(64);
	int batt_volt;
	int batt_curr;

	// Get sensor data.
	max17050_get_property(MAX17050_VCELL, &batt_volt);
	max17050_get_property(MAX17050_Current, &batt_curr);

	// Set battery current draw
	if (batt_curr >= 0) {

		s_printf(amp, "+%d mA", batt_curr / 1000);

		lv_label_set_array_text(status_bar.battery_more, amp, 64);
		lv_label_set_style(status_bar.battery_more, &font20green_style);

		free(amp);
	}

	else {

		s_printf(amp, "-%d mA", (~batt_curr + 1) / 1000);

		lv_label_set_array_text(status_bar.battery_more, amp, 64);
		lv_label_set_style(status_bar.battery_more, &font20red_style);

		free(amp);
	}

	char *mvolt = (char*)malloc(64);
	s_printf(volt, "%d V", batt_volt);
	s_printf(mvolt, "%d V", batt_volt);
	mvolt[0] = '.';
	volt[strlen(volt) - 5] = '\0';
	strcat(volt, mvolt);

	lv_label_set_array_text(status_bar.battery_more_volt, volt, 64);
	lv_label_set_style(status_bar.battery_more_volt, &font20_style);

	free(volt);
	free(mvolt);

	// Read and display temperature
	char *temp = (char*)malloc(64);

	u16 soc_temp = 0;

	soc_temp = tmp451_get_soc_temp(false);

	// Enable fan if more than 46 oC.
	u32 soc_temp_dec = (soc_temp >> 8);
	if (soc_temp_dec > 51)
		set_fan_duty(102);
	else if (soc_temp_dec > 46)
		set_fan_duty(51);
	else if (soc_temp_dec < 40)
		set_fan_duty(0);
	
	// Create SoC temperature label
	if (soc_temp_dec > 51)
		s_printf(temp, "CPU %02d.%d#", soc_temp_dec, (soc_temp & 0xFF) / 10);
	else if (soc_temp_dec > 41)
		s_printf(temp, "CPU %02d.%d#", soc_temp_dec, (soc_temp & 0xFF) / 10);
	else if (soc_temp_dec < 40)
		s_printf(temp, "CPU %02d.%d#", soc_temp_dec, (soc_temp & 0xFF) / 10);


	lv_label_set_array_text(status_bar.temperature, temp, 64);
	lv_label_set_style(status_bar.temperature, &font20_style);

	free(temp);

}

// Static features brightness
static lv_obj_t *slider;

// Title bar and status info - Title, battery, time, date, current, volt and temperature display
static void create_title(lv_theme_t *th)
{
	// Create Title
	lv_obj_t *title = lv_label_create(lv_scr_act(), NULL);
	lv_obj_align(title, lv_scr_act(), LV_ALIGN_IN_TOP_LEFT, 35, 620);//15
	lv_label_set_text(title, "AtomNX" SYMBOL_ATOM "v0.05");
	lv_obj_set_auto_realign(title, true);

	static lv_style_t label_style;
	lv_style_copy(&label_style, &lv_style_plain);
	label_style.text.color = LV_COLOR_WHITE;
	label_style.text.font = &atomfont;
	lv_obj_set_style(title, &label_style);

	// Read out and display definition styles, tool battery, time and date
	static lv_style_t font12_style;
	lv_style_copy(&font12_style, &lv_style_plain);
	font12_style.text.color = LV_COLOR_WHITE;
	font12_style.text.font = &mabolt_12;

	// Create label for update task Battery icon
	lv_obj_t* symb_battery = lv_label_create(lv_scr_act(), NULL);
	lv_obj_set_pos(symb_battery, 300, 615);
	lv_label_set_style(symb_battery, &labels_style);
	status_bar.batterysym = symb_battery;

	// Create Label for Task Update Battery %
	lv_obj_t* label_battery = lv_label_create(lv_scr_act(), NULL);
	lv_obj_set_pos(label_battery, 350, 620);
	status_bar.charging = label_battery;
	
	// Update task label Create time and date
	lv_obj_t *lbl_time_temp = lv_label_create(lv_scr_act(), NULL);
	lv_label_set_static_text(lbl_time_temp, "00.00.00 "" 00:00:0000");
	lv_obj_set_pos(lbl_time_temp, 430, 620);
	lv_label_set_style(lbl_time_temp, &font20_style);
	status_bar.time_date = lbl_time_temp;

	// Update label for task create Power Consumption and Volts
	lv_obj_t *label_voltage = lv_label_create(lv_scr_act(), NULL);
	lv_label_set_static_text(label_voltage, "+0 mA");
	lv_obj_set_pos(label_voltage, 1150, 620);
	status_bar.battery_more = label_voltage;

	lv_obj_t *label_volt = lv_label_create(lv_scr_act(), NULL);
	lv_label_set_static_text(label_volt, "0.000 V");
	lv_obj_set_pos(label_volt, 1050, 620);
	status_bar.battery_more_volt = label_volt;

	// Create label for task update Temperature
	lv_obj_t *label_temp = lv_label_create(lv_scr_act(), NULL);
	lv_label_set_static_text(label_temp, "CPU 00.0");
	lv_obj_set_pos(label_temp, 900, 620);
	status_bar.temperature = label_temp;

	// Label degree signs ° and C
	lv_obj_t *label_degrees = lv_label_create(lv_scr_act(), NULL);
	lv_label_set_text(label_degrees, "o");
	lv_obj_set_pos(label_degrees, 1000, 620);
	lv_label_set_style(label_degrees, &font12_style);

	lv_obj_t *label_c = lv_label_create(lv_scr_act(), NULL);
	lv_label_set_text(label_c, "C");
	lv_obj_set_pos(label_c, 1008, 620);
	lv_label_set_style(label_c, &font20_style);
}

// Brightness slider function
static lv_res_t ctrl_brightness(lv_obj_t *slider)
{
	// Set brightness
	int slider_light = lv_slider_get_value(slider);
	display_backlight_brightness(slider_light, 1000);

	// Read text from slider for hw.ini
	char *Lightness = (char*)malloc(64);
	s_printf(Lightness, "Brightness = %d", lv_slider_get_value(slider));

	// Save text to ini
	sd_mount();
	FIL fp;

	// Delete old hw.ini
	f_unlink("AtomNX/sys/hw.ini");

	// Check Config.ini available otherwise create
	if (f_stat("AtomNX/sys/hw.ini", NULL)) {

		f_open(&fp, "AtomNX/sys/hw.ini", FA_CREATE_NEW);
		f_close(&fp);

	}

	// Open file and save text
	f_open(&fp, "AtomNX/sys/hw.ini", FA_WRITE);

	f_puts(Lightness, &fp);

	f_close(&fp);

	sd_unmount(false);
	return LV_RES_OK;
}

// Tabview Buttons
static lv_obj_t *btn = NULL;

static lv_obj_t *scr;//Tab view definition hekate load_main_menu under  // Create screen container.
static lv_obj_t *tv;//Tab view definition hekate load_main_menu under // Add tabview page to screen. Required for AMS Version and Payload Tab

// Create Tools Tab
void create_tools_tab(lv_theme_t *th, lv_obj_t *parent)
{
	lv_page_set_scrl_layout(parent, LV_LAYOUT_OFF);
	lv_page_set_scrl_fit(parent, false, false);
	lv_page_set_scrl_height(parent, 620);

	// Declare styles for image buttons
	static lv_style_t style_pr;
	lv_style_copy(&style_pr, &lv_style_plain);
	style_pr.image.color = LV_COLOR_BLACK;
	style_pr.image.intense = LV_OPA_50;
	style_pr.text.color = LV_COLOR_HEX3(0xaaa);

	// Definition static lv_obj img zero for each button
	lv_img_dsc_t *img = NULL;

	sd_mount();

	u32 labels_y = 140;

	// System Tools
	lv_obj_t *label_sys = lv_label_create(parent, NULL);
	lv_label_set_text(label_sys, SYMBOL_SETTINGS" System Tools");
	lv_obj_set_pos(label_sys, 420, 100);
	lv_label_set_style(label_sys, &labels_style);

	lv_obj_t *btn_sys = lv_cont_create(parent, NULL);
	lv_obj_set_pos(btn_sys, 420, 140);
	lv_obj_set_size(btn_sys, 400, 450);
	lv_cont_set_layout(btn_sys, LV_LAYOUT_COL_L);

	// Try to get logo UMS SD button
	btn = lv_imgbtn_create(btn_sys, NULL);
	img = bmp_to_lvimg_obj("AtomNX/sys/gui/umssd.bmp");

	// Add button mask/radius and align icon.
	lv_obj_set_size(btn, 100, 100);
	lv_imgbtn_set_style(btn, LV_BTN_STATE_PR, &style_pr);
	lv_imgbtn_set_src(btn, LV_BTN_STATE_REL, img);
	lv_imgbtn_set_src(btn, LV_BTN_STATE_PR, img);
	lv_obj_set_pos(btn, 540, 185);
	lv_btn_set_action(btn, LV_BTN_ACTION_CLICK, action_ums_sd);

	lv_obj_t *label = lv_label_create(parent, NULL);
	lv_label_set_recolor(label, true);
	lv_label_set_text(label, "UMS SD Card\n#FF8000 Read/Write.#");
	lv_obj_set_pos(label, 540, 300);
	lv_label_set_style(label, &font20_style);


	// Try to get logo Test Button
	/*btn = lv_imgbtn_create(btn_sys, NULL);
	img = bmp_to_lvimg_obj("argon/sys/logos-gui/fileman.bmp");

	// Add button mask/radius and align icon.
	lv_obj_set_size(btn, 100, 100);
	lv_imgbtn_set_style(btn, LV_BTN_STATE_PR, &style_pr);
	lv_imgbtn_set_src(btn, LV_BTN_STATE_REL, img);
	lv_imgbtn_set_src(btn, LV_BTN_STATE_PR, img);
	lv_btn_set_action(btn, LV_BTN_ACTION_CLICK, ctrl_filemanager);//File Manager start

	label = lv_label_create(parent, NULL);
	lv_label_set_text(label, "Filemanager");
	lv_obj_set_pos(label, 540, 540);
	lv_label_set_style(label, &font20_style);*/


	// Power off tools
	lv_obj_t *power_label = lv_label_create(parent, NULL);
	lv_label_set_text(power_label, SYMBOL_POWER" Power Tools");
	lv_obj_set_pos(power_label, 60, 100);
	lv_label_set_style(power_label, &labels_style);

	lv_obj_t *btn_cont = lv_cont_create(parent, NULL);
	lv_obj_set_pos(btn_cont, 60, 140);
	lv_obj_set_size(btn_cont, 350, 450);
	lv_cont_set_layout(btn_cont, LV_LAYOUT_COL_L);

	// Try to get logo Button Power off
	btn = lv_imgbtn_create(btn_cont, NULL);
	img = bmp_to_lvimg_obj("AtomNX/sys/gui/power.bmp");

	// Add button mask/radius and align icon.
	lv_obj_set_size(btn, 100, 100);
	lv_imgbtn_set_style(btn, LV_BTN_STATE_PR, &style_pr);
	lv_imgbtn_set_src(btn, LV_BTN_STATE_REL, img);
	lv_imgbtn_set_src(btn, LV_BTN_STATE_PR, img);
	lv_btn_set_action(btn, LV_BTN_ACTION_CLICK, poweroff);

	label = lv_label_create(parent, NULL);
	lv_label_set_text(label, "Power off");
	lv_obj_set_pos(label, 180, 185);
	lv_label_set_style(label, &font20_style);

	// Try to get logo Button Reboot RCM or OFW
	btn = lv_imgbtn_create(btn_cont, NULL);
	img = bmp_to_lvimg_obj("AtomNX/sys/gui/power.bmp");

	// Add button mask/radius and align icon.
	lv_obj_set_size(btn, 100, 100);
	lv_imgbtn_set_style(btn, LV_BTN_STATE_PR, &style_pr);
	lv_imgbtn_set_src(btn, LV_BTN_STATE_REL, img);
	lv_imgbtn_set_src(btn, LV_BTN_STATE_PR, img);
	lv_btn_set_action(btn, LV_BTN_ACTION_CLICK, _create_rcm_ofw_reboot);

	label = lv_label_create(parent, NULL);
	lv_label_set_text(label, "Reboot RCM or OFW");
	lv_obj_set_pos(label, 180, 300);
	lv_label_set_style(label, &font20_style);

	// Try to get logo Button Reload APP
	btn = lv_imgbtn_create(btn_cont, NULL);
	img = bmp_to_lvimg_obj("AtomNX/sys/gui/power.bmp");

	// Add button mask/radius and align icon.
	lv_obj_set_size(btn, 100, 100);
	lv_imgbtn_set_style(btn, LV_BTN_STATE_PR, &style_pr);
	lv_imgbtn_set_src(btn, LV_BTN_STATE_REL, img);
	lv_imgbtn_set_src(btn, LV_BTN_STATE_PR, img);
	lv_btn_set_action(btn, LV_BTN_ACTION_CLICK, _create_mbox_reload);

	label = lv_label_create(parent, NULL);
	lv_label_set_text(label, "Reload Menu");
	lv_obj_set_pos(label, 180, 410);
	lv_label_set_style(label, &font20_style);

	// Configurations tools
	lv_obj_t *cfgtools_label = lv_label_create(parent, NULL);
	lv_label_set_text(cfgtools_label, SYMBOL_SETTINGS" Configuration Tools");
	lv_obj_set_pos(cfgtools_label, 850, labels_y);
	lv_label_set_style(cfgtools_label, &labels_style);

	lv_obj_t *btn_cfgtools = lv_cont_create(parent, NULL);
	lv_obj_set_pos(btn_cfgtools, 850, labels_y + 40);
	lv_obj_set_size(btn_cfgtools, 400, 450);
	lv_cont_set_layout(btn_cfgtools, LV_LAYOUT_COL_L);

	// Try to get logo RTC Button
	btn = lv_imgbtn_create(btn_cfgtools, NULL);
	img = bmp_to_lvimg_obj("AtomNX/sys/gui/rtc.bmp");

	// Add button mask/radius and align icon.
	lv_obj_set_size(btn, 100, 100);
	lv_imgbtn_set_style(btn, LV_BTN_STATE_PR, &style_pr);
	lv_imgbtn_set_src(btn, LV_BTN_STATE_REL, img);
	lv_imgbtn_set_src(btn, LV_BTN_STATE_PR, img);
	lv_btn_set_action(btn, LV_BTN_ACTION_CLICK, ctrl_rtctime);

	label = lv_label_create(parent, NULL);
	lv_label_set_text(label, "RTC Time and Date");
	lv_obj_set_pos(label, 970, 225);
	lv_label_set_style(label, &font20_style);

	// Info Section
	lv_obj_t *info_label = lv_label_create(parent, NULL);
	lv_label_set_text(info_label, SYMBOL_INFO" Information");
	lv_obj_set_pos(info_label, 850, 370);
	lv_label_set_style(info_label, &labels_style);

	// Try to get logo Info Button
	btn = lv_imgbtn_create(parent, NULL);
	img = bmp_to_lvimg_obj("AtomNX/sys/gui/about.bmp");

	// Add button mask/radius and align icon.															*********************** FIX AUTO CLOSE DUPLICATES
	lv_obj_set_size(btn, 100, 100);
	lv_imgbtn_set_style(btn, LV_BTN_STATE_PR, &style_pr);
	lv_imgbtn_set_src(btn, LV_BTN_STATE_REL, img);
	lv_imgbtn_set_src(btn, LV_BTN_STATE_PR, img);
	lv_obj_set_pos(btn, 980, 420);
	lv_btn_set_action(btn, LV_BTN_ACTION_CLICK, ctrl_info);

	label = lv_label_create(parent, NULL);
	lv_label_set_text(label, "Info");
	lv_obj_set_pos(label, 1155, 460);
	lv_label_set_style(label, &font20_style);


	// Try to get logo HW Info Button
	btn = lv_imgbtn_create(parent, NULL);
	img = bmp_to_lvimg_obj("AtomNX/sys/gui/about.bmp");

	// Add button mask/radius and align icon.
	lv_obj_set_size(btn, 100, 100);
	lv_imgbtn_set_style(btn, LV_BTN_STATE_PR, &style_pr);
	lv_imgbtn_set_src(btn, LV_BTN_STATE_REL, img);
	lv_imgbtn_set_src(btn, LV_BTN_STATE_PR, img);
	lv_obj_set_pos(btn, 855, 420);
	lv_btn_set_action(btn, LV_BTN_ACTION_CLICK, create_win_info);

	label = lv_label_create(parent, NULL);
	lv_label_set_text(label, "Hardware\nInfo");
	lv_obj_set_pos(label, 970, 450);
	lv_label_set_style(label, &font20_style);

	sd_unmount(false);

	// Brightness slider
	u32 slider_value = (PWM(PWM_CONTROLLER_PWM_CSR_0) >> 16) & 0xFF;

	// Create styles
	static lv_style_t style_bg;
	lv_style_copy(&style_bg, &lv_style_pretty);
	style_bg.body.main_color = LV_COLOR_BLACK;
	style_bg.body.grad_color = LV_COLOR_GRAY;
	style_bg.body.radius = LV_RADIUS_CIRCLE;
	style_bg.body.border.color = LV_COLOR_WHITE;

	static lv_style_t style_indic;
	lv_style_copy(&style_indic, &lv_style_pretty_color);
	style_indic.body.radius = LV_RADIUS_CIRCLE;
	style_indic.body.shadow.width = 8;
	style_indic.body.shadow.color = style_indic.body.main_color;
	style_indic.body.padding.left = 3;
	style_indic.body.padding.right = 3;
	style_indic.body.padding.top = 3;
	style_indic.body.padding.bottom = 3;

	static lv_style_t style_knob;
	lv_style_copy(&style_knob, &lv_style_pretty);
	style_knob.body.radius = LV_RADIUS_CIRCLE;
	style_knob.body.opa = LV_OPA_70;
	style_knob.body.padding.top = 10;
	style_knob.body.padding.bottom = 10;

	// Create a slider
	slider = lv_slider_create(parent, NULL);
	lv_slider_set_style(slider, LV_SLIDER_STYLE_BG, &style_bg);
	lv_slider_set_style(slider, LV_SLIDER_STYLE_INDIC, &style_indic);
	lv_slider_set_style(slider, LV_SLIDER_STYLE_KNOB, &style_knob);
	lv_obj_set_pos(slider, 120, 20);
	lv_obj_set_size(slider, 250, 35);

	// Set minimum and maximum values of slider and slider_value from above
	lv_slider_set_range(slider, 10, 200);
	lv_slider_set_value(slider, slider_value);
	lv_slider_set_action(slider, ctrl_brightness);

	label = lv_label_create(parent, NULL);
	lv_label_set_text(label, "Display brightness");
	lv_obj_set_pos(label, 90, 60);
	lv_label_set_style(label, &font20_style);

	// Create line
	lv_obj_t *line = lv_line_create(parent, NULL);
	static lv_point_t line_points[] = { {360, 20}, {360, LV_VER_RES_MAX - 120} };
	lv_line_set_points(line, line_points, 2);
	lv_line_set_style(line, lv_theme_get_current()->line.decor);

	lv_obj_t *line2 = lv_line_create(parent, NULL);
	static lv_point_t line2_points[] = { {790, 20}, {790, LV_VER_RES_MAX - 120} };
	lv_line_set_points(line2, line2_points, 2);
	lv_line_set_style(line2, lv_theme_get_current()->line.decor);

}

// Create Payload Tab features																			******************************* Create Payload Tab ***************************
void payload_full_path(const char *payload, char *result)
{
	strcpy(result, "AtomNX/payloads");
	strcat(result, "/");
	strcat(result, payload);
}

// Payload Logos
void payload_logo_path(const char *payload, char *result)
{
	char tmp[256];
	strcpy(tmp, "AtomNX/logos");
	strcat(tmp, "/");
	strcat(tmp, payload);

	strcpy(result, str_replace(tmp, ".bin", ".bmp"));
}

// Create payload tab entries
static bool create_payload_entries(lv_theme_t *th, lv_obj_t *parent, char *payloads, u32 group)
{
	lv_obj_t *btn = NULL;
	lv_obj_t *label = NULL;
	lv_img_dsc_t *img = NULL;

	u32 i = 8 * group;

	// Declare styles for payloads
	static lv_style_t style_pr;
	lv_style_copy(&style_pr, &lv_style_plain);
	style_pr.image.color = LV_COLOR_BLACK;
	style_pr.image.intense = LV_OPA_50;
	style_pr.text.color = LV_COLOR_HEX3(0xaaa);

	static lv_style_t no_img_label;
	lv_style_copy(&no_img_label, &lv_style_plain);
	no_img_label.text.font = &hekate_symbol_120;
	no_img_label.text.color = LV_COLOR_WHITE;

	// Style Label Button without Logo
	static lv_style_t no_plimg_label;
	lv_style_copy(&no_plimg_label, &lv_style_plain);
	no_plimg_label.text.font = &interui_20;
	no_plimg_label.text.color = LV_COLOR_WHITE;

	while (payloads[i * 256] && i < 8 * (group + 1))
	{
		char payload_path[256];
		char payload_logo[256];

		payload_full_path(&payloads[i * 256], payload_path);
		payload_logo_path(&payloads[i * 256], payload_logo);

		// Try to get payload logo
		img = bmp_to_lvimg_obj((const char*)payload_logo);

		if (!img)
		{
			// No logo present
			btn = lv_btn_create(parent, NULL);
			lv_obj_set_size(btn, 280, 280);
			lv_btn_set_style(btn, LV_BTN_STYLE_PR, &btn_transp_pr);
			lv_btn_set_style(btn, LV_BTN_STYLE_REL, &btn_transp_rel);

			label = lv_label_create(btn, NULL);
			lv_obj_set_style(label, &no_img_label);
			lv_label_set_text(label, SYMBOL_CLOCK);

			label = lv_label_create(btn, NULL);
			lv_obj_set_style(label, &no_plimg_label);
			lv_label_set_text(label, &payloads[i * 256]);
		}
		else
		{
			// Logo present
			btn = lv_imgbtn_create(parent, NULL);
			lv_imgbtn_set_style(btn, LV_BTN_STATE_PR, &style_pr);
			lv_imgbtn_set_src(btn, LV_BTN_STATE_REL, img);
			lv_imgbtn_set_src(btn, LV_BTN_STATE_PR, img);
		}

		// Payload path as invisible label
		label = lv_label_create(btn, NULL);
		lv_label_set_text(label, payload_path);
		lv_obj_set_style(label, &inv_label);

		lv_btn_set_action(btn, LV_BTN_ACTION_CLICK, launch_payload_btn);

		i++;
	}

	return true;
}

// Create payload tab
static bool create_tab_payload(lv_theme_t *th, lv_obj_t *par, char *payloads, u32 group, char *tabname)
{
	lv_obj_t *tab_payload = lv_tabview_add_tab(par, tabname);
	lv_page_set_sb_mode(tab_payload, LV_SB_MODE_OFF);

	lv_page_set_scrl_layout(tab_payload, LV_LAYOUT_OFF);
	lv_page_set_scrl_fit(tab_payload, false, false);
	lv_page_set_scrl_height(tab_payload, 620);

	lv_obj_t *page = lv_page_create(tab_payload, NULL);
	lv_obj_set_size(page, lv_obj_get_width(tab_payload), 620);
	lv_obj_align(page, tab_payload, LV_ALIGN_CENTER, 25, 0);
	lv_page_set_scrl_width(page, 0);

	// Horizontal grid layout
	lv_obj_t *plcnr = lv_page_get_scrl(page);
	lv_cont_set_layout(plcnr, LV_LAYOUT_PRETTY);
	lv_obj_set_size(plcnr, LV_HOR_RES_MAX * .95, lv_obj_get_height(page));

	lv_cont_set_style(plcnr, &lv_style_transp);

	create_payload_entries(th, plcnr, payloads, group);

	return true;
}

// Payload tab one or two, warning more than 16 PL
static bool render_payloads_tab(lv_theme_t *th, lv_obj_t *par)
{

	sd_mount();

	if (f_stat("AtomNX/payloads", NULL)) {

	}

	else {

		char *payloads = dirlist("AtomNX/payloads", "*.bin", false, false);
		u32 i = 0;
		u32 group = 0;

		while (payloads[i * 256])
		{
			if (i % 8 == 0)
			{
				if (group == 2)
				{
					lv_obj_t *label = lv_label_create(lv_tabview_get_tab(tv, 1), NULL);
					lv_label_set_text(label, "Attention: More than 16 payloads found! A maximum of 16 payloads are displayed!");
					lv_obj_set_style(label, &font20red_style);
					lv_obj_align(label, NULL, LV_ALIGN_IN_TOP_MID, 0, 0);

					break;
				}

				if (group == 0)
				{
					create_tab_payload(th, par, payloads, group, SYMBOL_DIRECTORY" Payload");
				}

				else
				{
					create_tab_payload(th, par, payloads, group, SYMBOL_DIRECTORY" Payload2");
				}

				group++;

			}
			i++;
		}
	}
	return true;

	sd_unmount(false);
}

// Create main menu
static void load_main_menu(lv_theme_t *th)
{
	// Initialize global styles.
	load_default_styles(th);

	// Create screen container.
	scr = lv_cont_create(NULL, NULL);
	lv_scr_load(scr);
	lv_cont_set_style(scr, th->bg);

	// Create base background and add a custom one if exists.
	lv_obj_t *cnr = lv_cont_create(scr, NULL);
	static lv_style_t base_bg_style;
	lv_style_copy(&base_bg_style, &lv_style_plain_color);
	base_bg_style.body.main_color = th->bg->body.main_color;
	base_bg_style.body.grad_color = base_bg_style.body.main_color;
	lv_cont_set_style(cnr, &base_bg_style);
	lv_obj_set_size(cnr, LV_HOR_RES, LV_VER_RES);

	if (hekate_bg)
	{
		lv_obj_t *img = lv_img_create(cnr, NULL);
		lv_img_set_src(img, hekate_bg);
	}

	// Add tabview page to screen.
	lv_obj_t *tv = lv_tabview_create(scr, NULL);
	if (hekate_bg)
	{
		lv_tabview_set_style(tv, LV_TABVIEW_STYLE_BTN_PR, &tabview_btn_pr);
		lv_tabview_set_style(tv, LV_TABVIEW_STYLE_BTN_TGL_PR, &tabview_btn_tgl_pr);
		// Move tabs buttons down
		lv_tabview_set_btns_pos(tv, LV_TABVIEW_BTNS_POS_BOTTOM);
	}
	lv_tabview_set_sliding(tv, false);
	lv_obj_set_size(tv, LV_HOR_RES, LV_VER_RES);

	// Read brightness from hw.ini and set
	sd_mount();
	#define MAXCHAR 100
	FIL fp;
	char info[MAXCHAR];

	// Check hw.ini available if not set basic brightness
	if (f_stat("AtomNX/sys/hw.ini", NULL)) {

		// Set brightness
		display_backlight_brightness(100, 1000);
	}

	else {

		f_open(&fp, "AtomNX/sys/hw.ini", FA_READ);
		while (f_gets(info, MAXCHAR, &fp)) {
			char *place;
			char *selection;
			selection = strstr(info, "Brightness =");
			place = strchr(selection, (int)'=');
			infotext = str_replace(place, "= ", "");

			int light = atoi(infotext);

			// Set brightness
			display_backlight_brightness(light, 1000);
		}
		f_close(&fp);
	}

	sd_unmount(false);

	// Create tabs
	render_payloads_tab(th, tv);
	lv_obj_t *tab_tools = lv_tabview_add_tab(tv, SYMBOL_TOOLS" Tools");
	create_tools_tab(th, tab_tools);// Tools Tab

	// Create status bar and Title
	create_title(th);

	// Create tasks.
	system_tasks.task.dram_periodic_comp = lv_task_create(minerva_periodic_training, EMC_PERIODIC_TRAIN_MS, LV_TASK_PRIO_HIGHEST, NULL);
	lv_task_ready(system_tasks.task.dram_periodic_comp);

	// Create Task Updates
	system_tasks.task.status_bar = lv_task_create(update_status, 5000, LV_TASK_PRIO_LOW, NULL);
	lv_task_ready(system_tasks.task.status_bar);

	lv_task_create(_check_sd_card_removed, 2000, LV_TASK_PRIO_LOWEST, NULL);
	 
	task_emmc_errors = lv_task_create(_nyx_emmc_issues, 2000, LV_TASK_PRIO_LOWEST, NULL);		// May Cause Error !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	lv_task_ready(task_emmc_errors);

}

// Initialize GUI
void gui_load_and_run()
{
	memset(&system_tasks, 0, sizeof(system_maintenance_tasks_t));

	lv_init();
	gfx_con.fillbg = 1;

	// Initialize framebuffer drawing functions.
	lv_disp_drv_t disp_drv;
	lv_disp_drv_init(&disp_drv);
	disp_drv.disp_flush = _disp_fb_flush;
	lv_disp_drv_register(&disp_drv);

	// Initialize Joy-Con.
	if (!n_cfg.jc_disable)
	{
		lv_task_t *task_jc_init_hw = lv_task_create(jc_init_hw, LV_TASK_ONESHOT, LV_TASK_PRIO_LOWEST, NULL);
		lv_task_once(task_jc_init_hw);
	}
	lv_indev_drv_t indev_drv_jc;
	lv_indev_drv_init(&indev_drv_jc);
	indev_drv_jc.type = LV_INDEV_TYPE_POINTER;
	indev_drv_jc.read = _jc_virt_mouse_read;
	memset(&jc_drv_ctx, 0, sizeof(jc_lv_driver_t));
	jc_drv_ctx.indev = lv_indev_drv_register(&indev_drv_jc);
	close_btn = NULL;

	// Initialize touch.
	touch_enabled = touch_power_on();
	lv_indev_drv_t indev_drv_touch;
	lv_indev_drv_init(&indev_drv_touch);
	indev_drv_touch.type = LV_INDEV_TYPE_POINTER;
	indev_drv_touch.read = _fts_touch_read;
	lv_indev_drv_register(&indev_drv_touch);
	touchpad.touch = false;

	// Initialize temperature sensor.
	tmp451_init();

	// Set theme
	lv_theme_t *th = lv_theme_cwad_init(0, NULL);

	lv_theme_set_current(th);

	// Create main menu
	load_main_menu(th);// Main Menu

	//Joycon Cursor 
	jc_drv_ctx.cursor = lv_img_create(lv_scr_act(), NULL);
	lv_img_set_src(jc_drv_ctx.cursor, &touch_cursor);
	lv_obj_set_opa_scale(jc_drv_ctx.cursor, LV_OPA_TRANSP);
	lv_obj_set_opa_scale_enable(jc_drv_ctx.cursor, true);

	// Check if sd card issues.
	if (sd_get_mode() == SD_1BIT_HS25)
	{
		lv_task_t *task_run_sd_errors = lv_task_create(_nyx_sd_card_issues, LV_TASK_ONESHOT, LV_TASK_PRIO_LOWEST, NULL);
		lv_task_once(task_run_sd_errors);
	}

	// Gui loop.
	if (h_cfg.t210b01)
	{
		// Minerva not supported on T210B01 yet. No power saving.
		while (true)
			lv_task_handler();
	}
	else
	{
		// Alternate DRAM frequencies. Saves 280 mW.
		while (true)
		{
			minerva_change_freq(FREQ_1600);  // Takes 295 us.

			lv_task_handler();

			minerva_change_freq(FREQ_800);   // Takes 80 us.
		}
	}
}
