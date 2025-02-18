/*
 *
 * Zinitix bt532 touch driver
 *
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */


#ifndef _LINUX_ZINITIX_TS_H
#define _LINUX_ZINITIX_TS_H


#define ZINITIX_TS_NAME	"zinitix_ts_device"

struct tsp_callbacks {
	void (*inform_charger)
		(struct tsp_callbacks *tsp_cb, bool mode);
};

struct zxt_ts_platform_data {
	u32		gpio_int;
	u32		gpio_scl;
	u32		gpio_sda;
	u32		gpio_ldo_en;
	u16		x_resolution;
	u16		y_resolution;
	u16		page_size;
	u8		orientation;
	int (*power) (bool on);
	void (*register_cb) (struct tsp_callbacks *);
	u32 key_code[4];
	int debug_mode;
};

extern struct class *sec_class;

#endif /* LINUX_BT532_TS_H */
