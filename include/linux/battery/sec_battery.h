/*
 * sec_battery.h
 * Samsung Mobile Battery Header
 *
 *
 * Copyright (C) 2012 Samsung Electronics, Inc.
 *
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __SEC_BATTERY_H
#define __SEC_BATTERY_H __FILE__

#include <linux/battery/sec_charging_common.h>
#if defined(CONFIG_ANDROID_ALARM_ACTIVATED)
#include <linux/android_alarm.h>
#else
#include <linux/alarmtimer.h>
#endif
#include <linux/wakelock.h>
#include <linux/workqueue.h>
#include <linux/proc_fs.h>
#include <linux/jiffies.h>
#include <linux/of_gpio.h>

#if defined(CONFIG_EXTCON)
#include <linux/extcon.h>
struct sec_battery_extcon_cable{
	struct extcon_specific_cable_nb extcon_nb;
	struct notifier_block batt_nb;
	int cable_index;
};
#endif /* CONFIG_EXTCON */

#define ADC_CH_COUNT		10
#define ADC_SAMPLE_COUNT	10

struct adc_sample_info {
	unsigned int cnt;
	int total_adc;
	int average_adc;
	int adc_arr[ADC_SAMPLE_COUNT];
	int index;
};

struct sec_battery_info {
	struct device *dev;
	sec_battery_platform_data_t *pdata;
	/* power supply used in Android */
	struct power_supply psy_bat;
	struct power_supply psy_usb;
	struct power_supply psy_ac;
	struct power_supply psy_wireless;
	struct power_supply psy_ps;
	unsigned int irq;

#if defined(CONFIG_EXTCON)
	struct sec_battery_extcon_cable extcon_cable_list[EXTCON_NONE];
#endif /* CONFIG_EXTCON */

	int status;
	int health;
	bool present;

	int voltage_now;		/* cell voltage (mV) */
	int voltage_avg;		/* average voltage (mV) */
	int voltage_ocv;		/* open circuit voltage (mV) */
	int current_now;		/* current (mA) */
	int current_avg;		/* average current (mA) */
	int current_max;		/* input current limit (mA) */
	int current_adc;

	unsigned int capacity;			/* SOC (%) */

	struct mutex adclock;
	struct adc_sample_info	adc_sample[ADC_CH_COUNT];

	/* keep awake until monitor is done */
	struct wake_lock monitor_wake_lock;
	struct workqueue_struct *monitor_wqueue;
	struct delayed_work monitor_work;
#ifdef CONFIG_SAMSUNG_BATTERY_FACTORY
	struct wake_lock lpm_wake_lock;
#endif
	unsigned int polling_count;
	unsigned int polling_time;
	bool polling_in_sleep;
	bool polling_short;

	struct delayed_work polling_work;
	struct alarm polling_alarm;
	ktime_t last_poll_time;

	/* event set */
	unsigned int event;
	unsigned int event_wait;
	struct alarm event_termination_alarm;
	ktime_t	last_event_time;

	/* battery check */
	unsigned int check_count;
	/* ADC check */
	unsigned int check_adc_count;
	unsigned int check_adc_value;

	/* time check */
	unsigned long charging_start_time;
	unsigned long charging_passed_time;
	unsigned long charging_next_time;
	unsigned long charging_fullcharged_time;

	/* temperature check */
	int temperature;	/* battery temperature */
	int temper_amb;		/* target temperature */

	int temp_adc;
	int temp_ambient_adc;

	int temp_highlimit_threshold;
	int temp_highlimit_recovery;
	int temp_high_threshold;
	int temp_high_recovery;
	int temp_low_threshold;
	int temp_low_recovery;

	unsigned int temp_highlimit_cnt;
	unsigned int temp_high_cnt;
	unsigned int temp_low_cnt;
	unsigned int temp_recover_cnt;

	/* charging */
	unsigned int charging_mode;
	bool is_recharging;
	bool is_jig_on;
	int cable_type;
	int muic_cable_type;
	int extended_cable_type;
	struct wake_lock cable_wake_lock;
	struct work_struct cable_work;
	struct wake_lock vbus_wake_lock;
	unsigned int full_check_cnt;
	unsigned int recharge_check_cnt;

	/* wireless charging enable */
	int wc_enable;
	int wc_status;

	int wire_status;

	/* wearable charging */
	int ps_enable;
	int ps_status;
	int ps_changed;

	/* test mode */
	int test_mode;
	bool factory_mode;
	bool slate_mode;

	int siop_level;
#if defined(CONFIG_SAMSUNG_BATTERY_ENG_TEST)
	int stability_test;
	int eng_not_full_status;
#endif
#ifdef CONFIG_SEC_BATTERY_PM_NOTIFY
	struct notifier_block pm_nb;
#endif
};

ssize_t sec_bat_show_attrs(struct device *dev,
		struct device_attribute *attr, char *buf);

ssize_t sec_bat_store_attrs(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count);

#define SEC_BATTERY_ATTR(_name)						\
{									\
	.attr = {.name = #_name, .mode = 0664},	\
	.show = sec_bat_show_attrs,					\
	.store = sec_bat_store_attrs,					\
}

/* event check */
#define EVENT_NONE			(0)
#define EVENT_2G_CALL			(0x1 << 0)
#define EVENT_3G_CALL			(0x1 << 1)
#define EVENT_MUSIC			(0x1 << 2)
#define EVENT_VIDEO			(0x1 << 3)
#define EVENT_BROWSER			(0x1 << 4)
#define EVENT_HOTSPOT			(0x1 << 5)
#define EVENT_CAMERA			(0x1 << 6)
#define EVENT_CAMCORDER			(0x1 << 7)
#define EVENT_DATA_CALL			(0x1 << 8)
#define EVENT_WIFI			(0x1 << 9)
#define EVENT_WIBRO			(0x1 << 10)
#define EVENT_LTE			(0x1 << 11)
#define EVENT_LCD			(0x1 << 12)
#define EVENT_GPS			(0x1 << 13)

#if 0
enum {
	POWER_SUPPLY_TYPE_WPC = 0xfff,
	POWER_SUPPLY_TYPE_MISC,
	POWER_SUPPLY_TYPE_CARDOCK,
	POWER_SUPPLY_TYPE_UARTOFF,
	POWER_SUPPLY_TYPE_WIRELESS,
	POWER_SUPPLY_TYPE_LAN_HUB,
	POWER_SUPPLY_TYPE_MHL_500,
	POWER_SUPPLY_TYPE_MHL_900,
	POWER_SUPPLY_TYPE_MHL_1500,
	POWER_SUPPLY_TYPE_MHL_USB,
	POWER_SUPPLY_TYPE_SMART_OTG,
	POWER_SUPPLY_TYPE_SMART_NOTG,
	POWER_SUPPLY_TYPE_POWER_SHARING
};
#endif

enum {
	BATT_RESET_SOC = 0,
	BATT_READ_RAW_SOC,
	BATT_READ_ADJ_SOC,
	BATT_TYPE,
	BATT_VFOCV,
	BATT_VOL_ADC,
	BATT_VOL_ADC_CAL,
	BATT_VOL_AVER,
	BATT_VOL_ADC_AVER,

	BATT_CURRENT_UA_NOW,
	BATT_CURRENT_UA_AVG,

	BATT_TEMP,
	BATT_TEMP_ADC,
	BATT_TEMP_AVER,
	BATT_TEMP_ADC_AVER,
	BATT_VF_ADC,
	BATT_SLATE_MODE,

	BATT_LP_CHARGING,
	SIOP_ACTIVATED,
	SIOP_LEVEL,
	BATT_CHARGING_SOURCE,
	FG_REG_DUMP,
	FG_RESET_CAP,
	FG_CAPACITY,
	AUTH,
	CHG_CURRENT_ADC,
	WC_ADC,
	WC_STATUS,
	WC_ENABLE,
	FACTORY_MODE,
	UPDATE,
	TEST_MODE,

	BATT_EVENT_CALL,
	BATT_EVENT_2G_CALL,
	BATT_EVENT_TALK_GSM,
	BATT_EVENT_3G_CALL,
	BATT_EVENT_TALK_WCDMA,
	BATT_EVENT_MUSIC,
	BATT_EVENT_VIDEO,
	BATT_EVENT_BROWSER,
	BATT_EVENT_HOTSPOT,
	BATT_EVENT_CAMERA,
	BATT_EVENT_CAMCORDER,
	BATT_EVENT_DATA_CALL,
	BATT_EVENT_WIFI,
	BATT_EVENT_WIBRO,
	BATT_EVENT_LTE,
	BATT_EVENT_LCD,
	BATT_EVENT_GPS,
	BATT_EVENT,
#if defined(CONFIG_SAMSUNG_BATTERY_ENG_TEST)
	BATT_TEST_CHARGE_CURRENT,
	BATT_STABILITY_TEST,
#endif
#if defined(CONFIG_SPRD_2713_POWER)
	BATT_2713_BATTERY_VOLTAGE,
	BATT_2713_BATTERY_ADC,
#endif
};

#ifdef CONFIG_OF
extern void adc_init(struct platform_device *pdev, struct sec_battery_info *battery);
extern int adc_read(struct sec_battery_info *battery, int channel);
extern void board_battery_init(struct platform_device *pdev, struct sec_battery_info *battery);
extern void cable_initial_check(struct sec_battery_info *battery);
extern bool sec_bat_check_jig_status(void);
extern void adc_exit(struct sec_battery_info *battery);
extern int sec_bat_check_cable_callback(struct sec_battery_info *battery);
extern bool sec_bat_check_cable_result_callback(int cable_type);
extern bool sec_bat_check_callback(struct sec_battery_info *battery);

/* set callback function pointer in platform_data object for DT
   and link battery data*/
extern void board_battery_init_link(sec_battery_platform_data_t *pdata);
#endif

#endif /* __SEC_BATTERY_H */
