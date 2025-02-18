/*
 * Copyright (C) 2012 Spreadtrum Communications Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/irq.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <asm/io.h>
#include <linux/file.h>
#include <mach/dma.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/of_gpio.h>

#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <mach/hardware.h>
#include <mach/board.h>
#include <linux/regulator/consumer.h>
#include <mach/regulator.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <mach/arch_misc.h>

#if defined(CONFIG_FLED_SM5701)
#include <linux/leds.h>
#include <linux/mfd/sm5701_core.h>
#endif

#if defined (CONFIG_ARCH_SC8825)
#include <mach/i2c-sprd.h>
#define SENSOR_I2C_ID                        1
#elif defined (CONFIG_ARCH_SC8810)
#include <mach/i2c-sc8810.h>
#elif defined (CONFIG_ARCH_SCX35)
#include <mach/i2c-sprd.h>
#include <mach/adi.h>
#define SENSOR_I2C_ID                        0
#endif

#include <video/sensor_drv_k.h>
#include "sensor_drv_sprd.h"
#include "power/sensor_power.h"
#include "csi2/csi_api.h"
#include "parse_hwinfo.h"

#define CAM_AHB_CLOCK_MCU_SLEEP_FEATURE
#ifdef CAM_AHB_CLOCK_MCU_SLEEP_FEATURE
#include <mach/cpuidle.h>
#endif
/* FIXME: Move to camera device platform data later */
/*#if defined(CONFIG_ARCH_SC8825)*/

#if defined(CONFIG_ARCH_SCX35)

#define REGU_NAME_CAMVIO     "vddcamio"

#if defined(CONFIG_MACH_KANAS_W) || defined(CONFIG_MACH_KANAS_TD) || defined (CONFIG_MACH_TSHARKWSAMSUNG)
#define REGU_NAME_SUB_CAMDVDD  "vddcamd"
#define REGU_NAME_CAMMOT       "vddcammot"

#define REGU_NAME_CAMAVDD	"vddcama"
#define REGU_NAME_CAMDVDD	"vddcamd"
#elif defined(CONFIG_MACH_CORE3_W)
#define REGU_NAME_SUB_CAMDVDD  "vddcamd"
#define REGU_NAME_CAMAVDD    "vddcama"
#define REGU_NAME_CAMDVDD    "vddcamd"
#define REGU_NAME_CAMMOT       "vddcammot"
#elif defined(CONFIG_MACH_YOUNG23GDTV)
#ifdef REGU_NAME_CAMVIO
#undef REGU_NAME_CAMVIO
#define REGU_NAME_CAMVIO     "vddcamd"
#endif
#define REGU_NAME_SUB_CAMDVDD  "vddcamd"
#define REGU_NAME_CAMAVDD    "vddcama"
#define REGU_NAME_CAMDVDD    "vddcamio"
#define REGU_NAME_CAMMOT       "vddcama"
#elif defined(CONFIG_MACH_KIRAN)
#ifdef REGU_NAME_CAMVIO
#undef REGU_NAME_CAMVIO
#define REGU_NAME_CAMVIO     "vddcamd"
#endif
#define REGU_NAME_SUB_CAMDVDD  "vddwifipa"
#define REGU_NAME_CAMAVDD    "vddcama"
#define REGU_NAME_CAMDVDD    "vddcamio"
#define REGU_NAME_CAMMOT       "vddcama"
#else
#define REGU_NAME_SUB_CAMDVDD  "vddcamd"
#define GPIO_SUB_SENSOR_RESET        GPIO_SENSOR_RESET
#define REGU_NAME_CAMAVDD    "vddcama"
#define REGU_NAME_CAMDVDD    "vddcamd"
#define REGU_NAME_CAMMOT       "vddcammot"
#endif

#define SENSOR_CLK           "clk_sensor"

#else

#define REGU_NAME_CAMAVDD    "vddcama"
#define REGU_NAME_CAMVIO     "vddcamio"
#define REGU_NAME_CAMDVDD    "vddcamcore"
#define REGU_NAME_CAMMOT     "vddcammot"
#define SENSOR_CLK           "ccir_mclk"

#endif

/*#endif*/

#define DEBUG_SENSOR_DRV
#ifdef  DEBUG_SENSOR_DRV
#define SENSOR_PRINT                      pr_info
#else
#define SENSOR_PRINT(...)
#endif
#define SENSOR_PRINT_ERR                  printk
#define SENSOR_PRINT_HIGH                 printk

#define SENSOR_K_SUCCESS                  0
#define SENSOR_K_FAIL                     (-1)
#define SENSOR_K_FALSE                    0
#define SENSOR_K_TRUE                     1


#define LOCAL                             static
//#define LOCAL

#define PNULL                             ((void *)0)

#define NUMBER_MAX                        0x7FFFFFF

#define SENSOR_MINOR                      MISC_DYNAMIC_MINOR
#define SLEEP_MS(ms)                      msleep(ms)

#define SENSOR_I2C_OP_TRY_NUM             4
#define SENSOR_CMD_BITS_8                 1
#define SENSOR_CMD_BITS_16                2
#define SENSOR_I2C_VAL_8BIT               0x00
#define SENSOR_I2C_VAL_16BIT              0x01
#define SENSOR_I2C_REG_8BIT               (0x00 << 1)
#define SENSOR_I2C_REG_16BIT              (0x01 << 1)
#define SENSOR_I2C_CUSTOM                 (0x01 << 2)
#define SENSOR_LOW_EIGHT_BIT              0xff

#define SENSOR_WRITE_DELAY                0xffff
#define DEBUG_STR                         "Error L %d, %s \n"
#define DEBUG_ARGS                        __LINE__,__FUNCTION__
#define SENSOR_MCLK_SRC_NUM               4
#define SENSOR_MCLK_DIV_MAX               4
#define ABS(a)                            ((a) > 0 ? (a) : -(a))
#define SENSOR_LOWEST_ADDR                0x800
#define SENSOR_ADDR_INVALID(addr)         ((uint32_t)(addr) < SENSOR_LOWEST_ADDR)

#define SENSOR_CHECK_ZERO(a)                                      \
	do {                                                       \
		if (SENSOR_ADDR_INVALID(a)) {                       \
			printk("SENSOR, zero pointer \n");           \
			printk(DEBUG_STR, DEBUG_ARGS);               \
			return -EFAULT;                              \
		}                                                   \
	} while(0)

#define SENSOR_CHECK_ZERO_VOID(a)                                 \
	do {                                                       \
		if (SENSOR_ADDR_INVALID(a)) {                       \
			printk("SENSOR, zero pointer \n");           \
			printk(DEBUG_STR, DEBUG_ARGS);               \
			return;                                      \
		}                                                   \
	} while(0)

typedef struct SN_MCLK {
	int        clock;
	char       *src_name;
} SN_MCLK;


struct sensor_mem_tag {
	void    *buf_ptr;
	size_t  size;
};

struct sensor_module {
	uint32_t                        sensor_id;
	uint32_t                        sensor_mclk;
	uint32_t                        iopower_on_count;
	uint32_t                        avddpower_on_count;
	uint32_t                        dvddpower_on_count;
	uint32_t                        motpower_on_count;
	uint32_t                        mipi_on;
	struct mutex                    sensor_lock;
	struct clk                      *ccir_clk;
	struct clk                      *mipi_clk;
	struct i2c_client               *cur_i2c_client;
	struct regulator                *camvio_regulator;
	struct regulator                *camavdd_regulator;
	struct regulator                *camdvdd_regulator;
	struct regulator                *cammot_regulator;
	struct i2c_driver               sensor_i2c_driver;
	struct sensor_mem_tag           sensor_mem;
	unsigned                        pin_main_reset;
	unsigned                        pin_sub_reset;
	unsigned                        pin_main_pd;
	unsigned                        pin_sub_pd;
	atomic_t                        open_count;
};

LOCAL const SN_MCLK                     c_sensor_mclk_tab[SENSOR_MCLK_SRC_NUM] = {
						{96, "clk_96m"},
						{77, "clk_76m8"},
						{48, "clk_48m"},
						{26, "ext_26m"}
};
LOCAL const unsigned short              c_sensor_main_default_addr_list[] =
						{SENSOR_MAIN_I2C_ADDR, SENSOR_SUB_I2C_ADDR, I2C_CLIENT_END};
LOCAL const struct i2c_device_id        c_sensor_device_id[] = {
						{SENSOR_MAIN_I2C_NAME, 0},
						{SENSOR_SUB_I2C_NAME, 1},
						{}
};

LOCAL struct sensor_module * s_p_sensor_mod = PNULL;
SENSOR_PROJECT_FUNC_T s_sensor_project_func = {PNULL};

uint32_t flash_torch_status=0;

LOCAL void* _sensor_k_kmalloc(size_t size)
{
	if (SENSOR_ADDR_INVALID(s_p_sensor_mod)) {        \
		printk("SENSOR, zero pointer \n");         \
		printk(DEBUG_STR, DEBUG_ARGS);             \
		return PNULL;                              \
	}

	if(PNULL == s_p_sensor_mod->sensor_mem.buf_ptr) {
		s_p_sensor_mod->sensor_mem.buf_ptr = vzalloc(size);
		if(PNULL != s_p_sensor_mod->sensor_mem.buf_ptr) {
			s_p_sensor_mod->sensor_mem.size = size;
		}
		return s_p_sensor_mod->sensor_mem.buf_ptr;

	} else if (size <= s_p_sensor_mod->sensor_mem.size) {
		return s_p_sensor_mod->sensor_mem.buf_ptr;

	} else {
		//realloc memory
		vfree(s_p_sensor_mod->sensor_mem.buf_ptr);
		s_p_sensor_mod->sensor_mem.buf_ptr = PNULL;
		s_p_sensor_mod->sensor_mem.size = 0;
		s_p_sensor_mod->sensor_mem.buf_ptr = vzalloc(size);
		if (PNULL != s_p_sensor_mod->sensor_mem.buf_ptr) {
			s_p_sensor_mod->sensor_mem.size = size;
		}

	return s_p_sensor_mod->sensor_mem.buf_ptr;
	}
}

LOCAL void* _sensor_k_kzalloc(size_t size)
{
	void *ptr = _sensor_k_kmalloc(size);

	return ptr;
}

LOCAL void _sensor_k_kfree(void *p)
{
	/* memory will not be free */
	return;
}

LOCAL uint32_t _sensor_K_get_curId(void)
{
	SENSOR_CHECK_ZERO(s_p_sensor_mod);
	return s_p_sensor_mod->sensor_id;
}
LOCAL int sensor_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int                res = 0;
	SENSOR_CHECK_ZERO(s_p_sensor_mod);

	SENSOR_PRINT_HIGH(KERN_INFO "SENSOR:sensor_probe E.\n");
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		SENSOR_PRINT_HIGH(KERN_INFO "SENSOR: %s: func check failed\n",
		       __FUNCTION__);
		res = -ENODEV;
		goto out;
	}
	s_p_sensor_mod->cur_i2c_client = client;

	SENSOR_PRINT_HIGH(KERN_INFO "sensor_probe, addr 0x%x\n",
		s_p_sensor_mod->cur_i2c_client->addr);
	return 0;
out:
	return res;
}

LOCAL int sensor_remove(struct i2c_client *client)
{
	return 0;
}

LOCAL int sensor_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	SENSOR_PRINT_HIGH("SENSOR_DRV: detect!");
	strcpy(info->type, client->name);
	return 0;
}


int sensor_k_set_pd_level(BOOLEAN power_level)
{

	switch (_sensor_K_get_curId()) {
	case SENSOR_MAIN:
		{
			SENSOR_PRINT_HIGH("SENSOR: pwdn %d,pin_main %d\n", power_level,s_p_sensor_mod->pin_main_pd);
			if (0 == power_level) {
				gpio_direction_output(s_p_sensor_mod->pin_main_pd, 0);

			} else {
				gpio_direction_output(s_p_sensor_mod->pin_main_pd, 1);
			}
			break;
		}
	case SENSOR_SUB:
		{
			SENSOR_PRINT_HIGH("SENSOR: pwdn %d,pin_sub %d\n", power_level,s_p_sensor_mod->pin_sub_pd);
			if (0 == power_level) {
				gpio_direction_output(s_p_sensor_mod->pin_sub_pd, 0);
			} else {
				gpio_direction_output(s_p_sensor_mod->pin_sub_pd, 1);
			}
			break;
		}
	default:
		break;
	}
	return SENSOR_K_SUCCESS;
}

LOCAL void _sensor_regulator_disable(uint32_t *power_on_count, struct regulator * ptr_cam_regulator)
{
	SENSOR_CHECK_ZERO_VOID(s_p_sensor_mod);

	if (*power_on_count > 0) {
		regulator_disable(ptr_cam_regulator);
		(*power_on_count)--;
	}
	SENSOR_PRINT("sensor pwr off done: cnt=0x%x, io=%x, av=%x, dv=%x, mo=%x \n", *power_on_count,
		s_p_sensor_mod->iopower_on_count,
		s_p_sensor_mod->avddpower_on_count,
		s_p_sensor_mod->dvddpower_on_count,
		s_p_sensor_mod->motpower_on_count);

}

LOCAL int _sensor_regulator_enable(uint32_t *power_on_count, struct regulator * ptr_cam_regulator)
{
	int err;
	SENSOR_CHECK_ZERO(s_p_sensor_mod);

	err = regulator_enable(ptr_cam_regulator);
	(*power_on_count)++;

	SENSOR_PRINT("sensor pwr on done: cnt=0x%x, io=%x, av=%x, dv=%x, mo=%x \n", *power_on_count,
		s_p_sensor_mod->iopower_on_count,
		s_p_sensor_mod->avddpower_on_count,
		s_p_sensor_mod->dvddpower_on_count,
		s_p_sensor_mod->motpower_on_count);

	return err;
}

int sensor_k_set_voltage_cammot(uint32_t cammot_val)
{
	int                  err = 0;
	uint32_t             volt_value = 0;
	SENSOR_CHECK_ZERO(s_p_sensor_mod);

	SENSOR_PRINT_HIGH("sensor set CAMMOT val %d\n",cammot_val);

	if (NULL == s_p_sensor_mod->cammot_regulator) {
		s_p_sensor_mod->cammot_regulator = regulator_get(NULL, REGU_NAME_CAMMOT);
		if (IS_ERR(s_p_sensor_mod->cammot_regulator)) {
			SENSOR_PRINT_ERR("SENSOR:get cammot.fail\n");
			return SENSOR_K_FAIL;
		}
	}

	switch (cammot_val) {
	case SENSOR_VDD_2800MV:
		err = regulator_set_voltage(s_p_sensor_mod->cammot_regulator,
					SENSOER_VDD_2800MV,
					SENSOER_VDD_2800MV);
		volt_value = SENSOER_VDD_2800MV;
		if (err)
			SENSOR_PRINT_ERR("SENSOR:set cammot 2.8 fail\n");
		break;
	case SENSOR_VDD_3000MV:
		err = regulator_set_voltage(s_p_sensor_mod->cammot_regulator,
					SENSOER_VDD_3000MV,
					SENSOER_VDD_3000MV);
		volt_value = SENSOER_VDD_3000MV;
		if (err)
			SENSOR_PRINT_ERR("SENSOR:set cammot 3.0 fail\n");
		break;
#if defined (CONFIG_ARCH_SCX35)
	case SENSOR_VDD_3300MV:
		err = regulator_set_voltage(s_p_sensor_mod->cammot_regulator,
					SENSOER_VDD_3300MV,
					SENSOER_VDD_3300MV);
		volt_value = SENSOER_VDD_3300MV;
		if (err)
			SENSOR_PRINT_ERR("SENSOR:set cammot 3.3 fail\n");
		break;
#else
	case SENSOR_VDD_2500MV:
		err = regulator_set_voltage(s_p_sensor_mod->cammot_regulator,
					SENSOER_VDD_2500MV,
					SENSOER_VDD_2500MV);
		volt_value = SENSOER_VDD_2500MV;
		if (err)
			SENSOR_PRINT_ERR("SENSOR:set cammot 2.5 fail\n");
		break;
#endif
	case SENSOR_VDD_1800MV:
		err = regulator_set_voltage(s_p_sensor_mod->cammot_regulator,
					SENSOER_VDD_1800MV,
					SENSOER_VDD_1800MV);
		volt_value = SENSOER_VDD_1800MV;
		if (err)
			SENSOR_PRINT_ERR("SENSOR:set cammot 1.8 fail\n");
		break;
	case SENSOR_VDD_CLOSED:
	case SENSOR_VDD_UNUSED:
	default:
		volt_value = 0;
		break;
	}
	if (err) {
		SENSOR_PRINT_ERR("SENSOR:set cammot err!.\n");
		return SENSOR_K_FAIL;
	}
	if (0 != volt_value) {
		err = _sensor_regulator_enable(&s_p_sensor_mod->motpower_on_count, s_p_sensor_mod->cammot_regulator);
		if (err) {
			regulator_put(s_p_sensor_mod->cammot_regulator);
			s_p_sensor_mod->cammot_regulator = NULL;
			SENSOR_PRINT_ERR("SENSOR:can't en cammot.\n");
			return SENSOR_K_FAIL;
		}
	} else {
		_sensor_regulator_disable(&s_p_sensor_mod->motpower_on_count, s_p_sensor_mod->cammot_regulator);
		regulator_put(s_p_sensor_mod->cammot_regulator);
		s_p_sensor_mod->cammot_regulator = NULL;
		SENSOR_PRINT("SENSOR:dis cammot.\n");
	}

	return SENSOR_K_SUCCESS;
}

int sensor_k_set_voltage_avdd(uint32_t avdd_val)
{
	int                err = 0;
	uint32_t           volt_value = 0;
	SENSOR_CHECK_ZERO(s_p_sensor_mod);

	SENSOR_PRINT_HIGH("sensor set AVDD val %d\n",avdd_val);

	if (NULL == s_p_sensor_mod->camavdd_regulator) {
		s_p_sensor_mod->camavdd_regulator = regulator_get(NULL, REGU_NAME_CAMAVDD);
		if (IS_ERR(s_p_sensor_mod->camavdd_regulator)) {
			SENSOR_PRINT_ERR("SENSOR:get avdd.fail\n");
			return SENSOR_K_FAIL;
		}
	}
	switch (avdd_val) {
	case SENSOR_VDD_2800MV:
		err = regulator_set_voltage(s_p_sensor_mod->camavdd_regulator,
					SENSOER_VDD_2800MV,
					SENSOER_VDD_2800MV);
		volt_value = SENSOER_VDD_2800MV;
		if (err)
			SENSOR_PRINT_ERR("SENSOR:set avdd to 2.8 fail\n");
		break;
	case SENSOR_VDD_3000MV:
		err = regulator_set_voltage(s_p_sensor_mod->camavdd_regulator,
					SENSOER_VDD_3000MV,
					SENSOER_VDD_3000MV);
		volt_value = SENSOER_VDD_3000MV;
		if (err)
			SENSOR_PRINT_ERR("SENSOR:set avdd to 3.0 fail\n");
		break;
	case SENSOR_VDD_2500MV:
		err = regulator_set_voltage(s_p_sensor_mod->camavdd_regulator,
					SENSOER_VDD_2500MV,
					SENSOER_VDD_2500MV);
		volt_value = SENSOER_VDD_2500MV;
		if (err)
			SENSOR_PRINT_ERR("SENSOR:set avdd to 2.5 fail\n");
		break;
	case SENSOR_VDD_1800MV:
		err = regulator_set_voltage(s_p_sensor_mod->camavdd_regulator,
					SENSOER_VDD_1800MV,
					SENSOER_VDD_1800MV);
		volt_value = SENSOER_VDD_1800MV;
		if (err)
			SENSOR_PRINT_ERR("SENSOR:set avdd to 1.8 fail\n");
		break;
	case SENSOR_VDD_CLOSED:
	case SENSOR_VDD_UNUSED:
	default:
		volt_value = 0;
		break;
	}
	if (err) {
		SENSOR_PRINT_ERR("SENSOR:set avdd err!.\n");
		return SENSOR_K_FAIL;
	}
	if (0 != volt_value) {
		err = _sensor_regulator_enable(&s_p_sensor_mod->avddpower_on_count, s_p_sensor_mod->camavdd_regulator);
		if (err) {
			regulator_put(s_p_sensor_mod->camavdd_regulator);
			s_p_sensor_mod->camavdd_regulator = NULL;
			SENSOR_PRINT_ERR("SENSOR:can't en avdd.\n");
			return SENSOR_K_FAIL;
		}
	} else {
		_sensor_regulator_disable(&s_p_sensor_mod->avddpower_on_count, s_p_sensor_mod->camavdd_regulator);
		regulator_put(s_p_sensor_mod->camavdd_regulator);
		s_p_sensor_mod->camavdd_regulator = NULL;
		SENSOR_PRINT("SENSOR:dis avdd.\n");
	}

	return SENSOR_K_SUCCESS;
}
#ifdef CONFIG_MACH_KIRAN
int sensor_k_set_voltage_dvdd(uint32_t dvdd_val, uint32_t sensor_id)
{
	int                  err = 0;
	uint32_t             volt_value = 0;
	SENSOR_CHECK_ZERO(s_p_sensor_mod);

	SENSOR_PRINT_HIGH("sensor set DVDD val %d\n",dvdd_val);


	switch (sensor_id) {
		case SENSOR_MAIN:
		{
			s_p_sensor_mod->camdvdd_regulator = regulator_get(NULL, REGU_NAME_CAMDVDD);
				break;
		}
		case SENSOR_SUB:
		{
			SENSOR_PRINT("SENSOR:_sensor_k_setvoltage_dvdd, dvdd_val=%d  this is sub camera \n", dvdd_val);
			s_p_sensor_mod->camdvdd_regulator = regulator_get(NULL, REGU_NAME_SUB_CAMDVDD);
				break;
		}
		default:
			s_p_sensor_mod->camdvdd_regulator = regulator_get(NULL, REGU_NAME_CAMDVDD);
			break;
	}
	if (IS_ERR(s_p_sensor_mod->camdvdd_regulator)) {
		SENSOR_PRINT_ERR("SENSOR:get dvdd fail\n");
		return SENSOR_K_FAIL;
	}

	switch (dvdd_val) {
#if defined (CONFIG_ARCH_SCX35)
	case SENSOR_VDD_1200MV:
		err = regulator_set_voltage(s_p_sensor_mod->camdvdd_regulator,
					SENSOER_VDD_1200MV,
					SENSOER_VDD_1200MV);
		volt_value = SENSOER_VDD_1200MV;
		if (err)
			SENSOR_PRINT_ERR("SENSOR:set dvdd to 1.2 fail\n");
		break;
#else
	case SENSOR_VDD_2800MV:
		err = regulator_set_voltage(s_p_sensor_mod->camdvdd_regulator,
					SENSOER_VDD_2800MV,
					SENSOER_VDD_2800MV);
		volt_value = SENSOER_VDD_2800MV;
		if (err)
			SENSOR_PRINT_ERR("SENSOR:set dvdd to 2.8 fail\n");
		break;
#endif

	case SENSOR_VDD_1800MV:
		err = regulator_set_voltage(s_p_sensor_mod->camdvdd_regulator,
					SENSOER_VDD_1800MV,
					SENSOER_VDD_1800MV);
		volt_value = SENSOER_VDD_1800MV;
		if (err)
			SENSOR_PRINT_ERR("SENSOR:set dvdd to 1.8 fail\n");
		break;
	case SENSOR_VDD_1500MV:
		err = regulator_set_voltage(s_p_sensor_mod->camdvdd_regulator,
					SENSOER_VDD_1500MV,
					SENSOER_VDD_1500MV);
		volt_value = SENSOER_VDD_1500MV;
		if (err)
			SENSOR_PRINT_ERR("SENSOR:set dvdd to 1.5 fail\n");
		break;
	case SENSOR_VDD_1300MV:
		err = regulator_set_voltage(s_p_sensor_mod->camdvdd_regulator,
					SENSOER_VDD_1300MV,
					SENSOER_VDD_1300MV);
		volt_value = SENSOER_VDD_1300MV;
		if (err)
			SENSOR_PRINT_ERR("SENSOR:set dvdd to 1.3 fail\n");
		break;


	case SENSOR_VDD_CLOSED:
	case SENSOR_VDD_UNUSED:
	default:
		volt_value = 0;
		break;
	}
	if (err) {
		SENSOR_PRINT_ERR("SENSOR:set dvdd err %d!.\n",err);
		return SENSOR_K_FAIL;
	}
	if (0 != volt_value) {
		err = _sensor_regulator_enable(&s_p_sensor_mod->dvddpower_on_count,  s_p_sensor_mod->camdvdd_regulator);
		if (err) {
			regulator_put(s_p_sensor_mod->camdvdd_regulator);
			s_p_sensor_mod->camdvdd_regulator = NULL;
			SENSOR_PRINT_ERR("SENSOR:can't en dvdd.\n");
			return SENSOR_K_FAIL;
		}
	} else {
		_sensor_regulator_disable(&s_p_sensor_mod->dvddpower_on_count,  s_p_sensor_mod->camdvdd_regulator);
		regulator_put(s_p_sensor_mod->camdvdd_regulator);
		s_p_sensor_mod->camdvdd_regulator = NULL;
		SENSOR_PRINT("SENSOR:dis dvdd.\n");
	}

	return SENSOR_K_SUCCESS;
}

#else
int sensor_k_set_voltage_dvdd(uint32_t dvdd_val)
{
	int                  err = 0;
	uint32_t             volt_value = 0;
	SENSOR_CHECK_ZERO(s_p_sensor_mod);

	SENSOR_PRINT_HIGH("sensor set DVDD val %d\n",dvdd_val);

	if (!s_p_sensor_mod->camdvdd_regulator) {
		switch (_sensor_K_get_curId()) {
			case SENSOR_MAIN:
			{
				s_p_sensor_mod->camdvdd_regulator = regulator_get(NULL, REGU_NAME_CAMDVDD);
					break;
			}
			case SENSOR_SUB:
			{
				SENSOR_PRINT("SENSOR:_sensor_k_setvoltage_dvdd, dvdd_val=%d  thi is sub camera \n", dvdd_val);
				s_p_sensor_mod->camdvdd_regulator = regulator_get(NULL, REGU_NAME_SUB_CAMDVDD);
					break;
			}
			default:
				s_p_sensor_mod->camdvdd_regulator = regulator_get(NULL, REGU_NAME_CAMDVDD);
				break;
		}
		if (IS_ERR(s_p_sensor_mod->camdvdd_regulator)) {
			SENSOR_PRINT_ERR("SENSOR:get dvdd fail\n");
			return SENSOR_K_FAIL;
		}
	}
	switch (dvdd_val) {
#if defined (CONFIG_ARCH_SCX35)
	case SENSOR_VDD_1200MV:
		err = regulator_set_voltage(s_p_sensor_mod->camdvdd_regulator,
					SENSOER_VDD_1200MV,
					SENSOER_VDD_1200MV);
		volt_value = SENSOER_VDD_1200MV;
		if (err)
			SENSOR_PRINT_ERR("SENSOR:set dvdd to 1.2 fail\n");
		break;
#else
	case SENSOR_VDD_2800MV:
		err = regulator_set_voltage(s_p_sensor_mod->camdvdd_regulator,
					SENSOER_VDD_2800MV,
					SENSOER_VDD_2800MV);
		volt_value = SENSOER_VDD_2800MV;
		if (err)
			SENSOR_PRINT_ERR("SENSOR:set dvdd to 2.8 fail\n");
		break;
#endif

	case SENSOR_VDD_1800MV:
		err = regulator_set_voltage(s_p_sensor_mod->camdvdd_regulator,
					SENSOER_VDD_1800MV,
					SENSOER_VDD_1800MV);
		volt_value = SENSOER_VDD_1800MV;
		if (err)
			SENSOR_PRINT_ERR("SENSOR:set dvdd to 1.8 fail\n");
		break;
	case SENSOR_VDD_1500MV:
		err = regulator_set_voltage(s_p_sensor_mod->camdvdd_regulator,
					SENSOER_VDD_1500MV,
					SENSOER_VDD_1500MV);
		volt_value = SENSOER_VDD_1500MV;
		if (err)
			SENSOR_PRINT_ERR("SENSOR:set dvdd to 1.5 fail\n");
		break;
	case SENSOR_VDD_1300MV:
		err = regulator_set_voltage(s_p_sensor_mod->camdvdd_regulator,
					SENSOER_VDD_1300MV,
					SENSOER_VDD_1300MV);
		volt_value = SENSOER_VDD_1300MV;
		if (err)
			SENSOR_PRINT_ERR("SENSOR:set dvdd to 1.3 fail\n");
		break;


	case SENSOR_VDD_CLOSED:
	case SENSOR_VDD_UNUSED:
	default:
		volt_value = 0;
		break;
	}
	if (err) {
		SENSOR_PRINT_ERR("SENSOR:set dvdd err %d!.\n",err);
		return SENSOR_K_FAIL;
	}
	if (0 != volt_value) {
		err = _sensor_regulator_enable(&s_p_sensor_mod->dvddpower_on_count,  s_p_sensor_mod->camdvdd_regulator);
		if (err) {
			regulator_put(s_p_sensor_mod->camdvdd_regulator);
			s_p_sensor_mod->camdvdd_regulator = NULL;
			SENSOR_PRINT_ERR("SENSOR:can't en dvdd.\n");
			return SENSOR_K_FAIL;
		}
	} else {
		_sensor_regulator_disable(&s_p_sensor_mod->dvddpower_on_count,  s_p_sensor_mod->camdvdd_regulator);
		regulator_put(s_p_sensor_mod->camdvdd_regulator);
		s_p_sensor_mod->camdvdd_regulator = NULL;
		SENSOR_PRINT("SENSOR:dis dvdd.\n");
	}

	return SENSOR_K_SUCCESS;
}
#endif
int sensor_k_set_voltage_iovdd(uint32_t iodd_val)
{
	int                    err = 0;
	uint32_t               volt_value = 0;
	SENSOR_CHECK_ZERO(s_p_sensor_mod);

	SENSOR_PRINT_HIGH("sensor set IOVDD val %d\n",iodd_val);

	if(NULL == s_p_sensor_mod->camvio_regulator) {
		s_p_sensor_mod->camvio_regulator = regulator_get(NULL, REGU_NAME_CAMVIO);
		if (IS_ERR(s_p_sensor_mod->camvio_regulator)) {
			SENSOR_PRINT_ERR("SENSOR:get camvio.fail\n");
			return SENSOR_K_FAIL;
		}
	}
	switch (iodd_val) {
	case SENSOR_VDD_2800MV:
		err = regulator_set_voltage(s_p_sensor_mod->camvio_regulator,
					SENSOER_VDD_2800MV,
					SENSOER_VDD_2800MV);
		volt_value = SENSOER_VDD_2800MV;
		if (err)
			SENSOR_PRINT_ERR("SENSOR:set camvio to 2.8 fail\n");
		break;

#if defined (CONFIG_ARCH_SCX35)
	case SENSOR_VDD_2500MV:
		err = regulator_set_voltage(s_p_sensor_mod->camvio_regulator,
					SENSOER_VDD_2500MV,
					SENSOER_VDD_2500MV);
		volt_value = SENSOER_VDD_2500MV;
		if (err)
			SENSOR_PRINT_ERR("SENSOR:set camvio to 2.5 fail\n");
		break;
	case SENSOR_VDD_1500MV:
		err = regulator_set_voltage(s_p_sensor_mod->camvio_regulator,
					SENSOER_VDD_1500MV,
					SENSOER_VDD_1500MV);
		volt_value = SENSOER_VDD_1500MV;
		if (err)
			SENSOR_PRINT_ERR("SENSOR:set camvio to 1.5 fail\n");
		break;
#else
	case SENSOR_VDD_3800MV:
		err = regulator_set_voltage(s_p_sensor_mod->camvio_regulator,
					SENSOER_VDD_3800MV,
					SENSOER_VDD_3800MV);
		volt_value = SENSOER_VDD_3800MV;
		if (err)
			SENSOR_PRINT_ERR("SENSOR:set camvio to 3.8 fail\n");
		break;

	case SENSOR_VDD_1200MV:
		err =
		    regulator_set_voltage(s_p_sensor_mod->camvio_regulator,
					  SENSOER_VDD_1200MV,
					  SENSOER_VDD_1200MV);
		volt_value = SENSOER_VDD_1200MV;
		if (err)
			SENSOR_PRINT_ERR("SENSOR:set camvio to 1.2 fail\n");
		break;

#endif

	case SENSOR_VDD_1800MV:
		err = regulator_set_voltage(s_p_sensor_mod->camvio_regulator,
					SENSOER_VDD_1800MV,
					SENSOER_VDD_1800MV);
		volt_value = SENSOER_VDD_1800MV;
		if (err)
			SENSOR_PRINT_ERR("SENSOR:set camvio to 1.8 fail\n");
		break;
	case SENSOR_VDD_CLOSED:
	case SENSOR_VDD_UNUSED:
	default:
		volt_value = 0;
		break;
	}
	if (err) {
		SENSOR_PRINT_ERR("SENSOR:set camvio err!.\n");
		return SENSOR_K_FAIL;
	}
	if (0 != volt_value) {
		err = _sensor_regulator_enable(&s_p_sensor_mod->iopower_on_count, s_p_sensor_mod->camvio_regulator);
		if (err) {
			regulator_put(s_p_sensor_mod->camvio_regulator);
			s_p_sensor_mod->camvio_regulator = NULL;
			SENSOR_PRINT_ERR("SENSOR:can't en camvio.\n");
			return SENSOR_K_FAIL;
		}
	} else {
		_sensor_regulator_disable(&s_p_sensor_mod->iopower_on_count, s_p_sensor_mod->camvio_regulator);
		regulator_put(s_p_sensor_mod->camvio_regulator);
		s_p_sensor_mod->camvio_regulator = NULL;
		SENSOR_PRINT("SENSOR:dis camvio.\n");
	}

	return SENSOR_K_SUCCESS;
}

LOCAL int _select_sensor_mclk(uint8_t clk_set, char **clk_src_name,
			uint8_t * clk_div)
{
	uint8_t               i = 0;
	uint8_t               j = 0;
	uint8_t               mark_src = 0;
	uint8_t               mark_div = 0;
	uint8_t               mark_src_tmp = 0;
	int                   clk_tmp = NUMBER_MAX;
	int                   src_delta = NUMBER_MAX;
	int                   src_delta_min = NUMBER_MAX;
	int                   div_delta_min = NUMBER_MAX;
	SENSOR_CHECK_ZERO(s_p_sensor_mod);

	SENSOR_PRINT("SENSOR sel mclk %d.\n", clk_set);
	if (clk_set > 96 || !clk_src_name || !clk_div) {
		return SENSOR_K_FAIL;
	}
	for (i = 0; i < SENSOR_MCLK_DIV_MAX; i++) {
		clk_tmp = (int)(clk_set * (i + 1));
		src_delta_min = NUMBER_MAX;
		for (j = 0; j < SENSOR_MCLK_SRC_NUM; j++) {
			src_delta = ABS(c_sensor_mclk_tab[j].clock - clk_tmp);
			if (src_delta < src_delta_min) {
				src_delta_min = src_delta;
				mark_src_tmp = j;
			}
		}
		if (src_delta_min < div_delta_min) {
			div_delta_min = src_delta_min;
			mark_src = mark_src_tmp;
			mark_div = i;
		}
	}
	SENSOR_PRINT("src %d, div=%d .\n", mark_src,
		mark_div);

	*clk_src_name = c_sensor_mclk_tab[mark_src].src_name;
	*clk_div = mark_div + 1;

	return SENSOR_K_SUCCESS;
}

int32_t _sensor_k_mipi_clk_en(struct device_node *dn)
{
	int                     ret = 0;

	SENSOR_CHECK_ZERO(s_p_sensor_mod);

	if (NULL == s_p_sensor_mod->mipi_clk) {
		s_p_sensor_mod->mipi_clk = parse_clk(dn,"clk_dcam_mipi");
	}

	if (IS_ERR(s_p_sensor_mod->mipi_clk)) {
		printk("SENSOR: get dcam mipi clk error \n");
		return -1;
	} else {
		ret = clk_enable(s_p_sensor_mod->mipi_clk);
		if (ret) {
			printk("SENSOR: enable dcam mipi clk error %d \n", ret);
			return -1;
		}
	}

	return ret;
}

int32_t _sensor_k_mipi_clk_dis(void)
{
	SENSOR_CHECK_ZERO(s_p_sensor_mod);

	if (s_p_sensor_mod->mipi_clk) {
		clk_disable(s_p_sensor_mod->mipi_clk);
		clk_put(s_p_sensor_mod->mipi_clk);
		s_p_sensor_mod->mipi_clk = NULL;
	}
	return 0;
}

int sensor_k_set_mclk(uint32_t mclk)
{
	struct clk            *clk_parent = NULL;
	int                   ret;
	char                  *clk_src_name = NULL;
	uint8_t               clk_div;
	SENSOR_CHECK_ZERO(s_p_sensor_mod);

	SENSOR_PRINT_HIGH("SENSOR: set mclk org = %d, clk = %d\n",
				s_p_sensor_mod->sensor_mclk, mclk);

	if ((0 != mclk) && (s_p_sensor_mod->sensor_mclk != mclk)) {
		if (mclk > SENSOR_MAX_MCLK) {
			mclk = SENSOR_MAX_MCLK;
		}
		if (s_p_sensor_mod->sensor_mclk) {
			clk_disable(s_p_sensor_mod->ccir_clk);
		}
		if (SENSOR_K_SUCCESS != _select_sensor_mclk((uint8_t) mclk, &clk_src_name, &clk_div)) {
			SENSOR_PRINT_ERR("SENSOR:Sensor_SetMCLK select clock source fail.\n");
			return -EINVAL;
		}
		SENSOR_PRINT("clk_src_name=%s, clk_div=%d \n", clk_src_name, clk_div);

		clk_parent = clk_get(NULL, clk_src_name);
		if (!clk_parent) {
			SENSOR_PRINT_ERR("###:clock: failed to get clock [%s] by clk_get()!\n", clk_src_name);
			return -EINVAL;
		}
		SENSOR_PRINT("clk_get clk_src_name=%s done\n", clk_src_name);

		ret = clk_set_parent(s_p_sensor_mod->ccir_clk, clk_parent);
		if (ret) {
			SENSOR_PRINT_ERR("###:clock: clk_set_parent() failed!parent \n");
			return -EINVAL;
		}
		SENSOR_PRINT("clk_set_parent s_ccir_clk=%s done\n", (char *)(s_p_sensor_mod->ccir_clk));

		ret = clk_set_rate(s_p_sensor_mod->ccir_clk, (mclk * SENOR_CLK_M_VALUE));
		if (ret) {
			SENSOR_PRINT_ERR("###:clock: clk_set_rate failed!\n");
			return -EINVAL;
		}
		SENSOR_PRINT("clk_set_rate s_ccir_clk=%s done\n", (char *)(s_p_sensor_mod->ccir_clk));

		ret = clk_enable(s_p_sensor_mod->ccir_clk);
		if (ret) {
			SENSOR_PRINT_ERR("###:clock: clk_enable() failed!\n");
		} else {
			SENSOR_PRINT("######ccir enable clk ok\n");
		}

		s_p_sensor_mod->sensor_mclk = mclk;
		SENSOR_PRINT("SENSOR: set mclk %d Hz.\n",
			s_p_sensor_mod->sensor_mclk);
	} else if (0 == mclk) {
		if (s_p_sensor_mod->sensor_mclk) {
			if (s_p_sensor_mod->ccir_clk) {
				clk_disable(s_p_sensor_mod->ccir_clk);
				SENSOR_PRINT("###sensor clk disable ok.\n");
			}

			s_p_sensor_mod->sensor_mclk = 0;
		}
		SENSOR_PRINT("SENSOR: Disable MCLK !!!");
	} else {
		SENSOR_PRINT("SENSOR: Do nothing !! ");
	}
	SENSOR_PRINT_HIGH("SENSOR: set mclk X\n");
	return 0;
}

LOCAL int _sensor_k_reset(uint32_t level, uint32_t width)
{
	SENSOR_PRINT("SENSOR:_sensor_k_reset, reset_val=%d  camera:%d (0:main 1:sub)\n",level, _sensor_K_get_curId());

	switch (_sensor_K_get_curId()) {
	case SENSOR_MAIN:
	{
		gpio_direction_output(s_p_sensor_mod->pin_main_reset, level);
		gpio_set_value(s_p_sensor_mod->pin_main_reset, level);
		SLEEP_MS(width);
		gpio_set_value(s_p_sensor_mod->pin_main_reset, !level);
		mdelay(1);
		break;
	}
	case SENSOR_SUB:
	{
		gpio_direction_output(s_p_sensor_mod->pin_sub_reset, level);
		gpio_set_value(s_p_sensor_mod->pin_sub_reset, level);
		SLEEP_MS(width);
		gpio_set_value(s_p_sensor_mod->pin_sub_reset, !level);
		mdelay(1);
		break;
	}
	default:
		break;
	}

	return SENSOR_K_SUCCESS;
}

int sensor_k_sensor_sel(uint32_t sensor_id)
{
	SENSOR_CHECK_ZERO(s_p_sensor_mod);
	s_p_sensor_mod->sensor_id = sensor_id;

	return SENSOR_K_SUCCESS;
}

int sensor_k_sensor_desel(uint32_t sensor_id)
{
	SENSOR_CHECK_ZERO(s_p_sensor_mod);
	s_p_sensor_mod->sensor_id = SENSOR_ID_MAX;

	SENSOR_PRINT_HIGH("-I2C %d OK.\n", sensor_id);

	return SENSOR_K_SUCCESS;
}

int sensor_k_set_rst_level(uint32_t plus_level)
{

	switch (_sensor_K_get_curId()) {
		case SENSOR_MAIN:
		{
			SENSOR_PRINT("sensor set rst lvl: lvl %d, rst pin %d \n", plus_level, s_p_sensor_mod->pin_main_reset);
			gpio_direction_output(s_p_sensor_mod->pin_main_reset, plus_level);
			gpio_set_value(s_p_sensor_mod->pin_main_reset, plus_level);
			break;
		}
		case SENSOR_SUB:
		{
			SENSOR_PRINT("sensor set rst lvl: lvl %d, rst pin %d \n", plus_level, s_p_sensor_mod->pin_sub_reset);
			gpio_direction_output(s_p_sensor_mod->pin_sub_reset, plus_level);
			gpio_set_value(s_p_sensor_mod->pin_sub_reset, plus_level);
			break;
		}
		default:
		break;
	}

	return SENSOR_K_SUCCESS;
}

LOCAL int _Sensor_K_ReadReg(SENSOR_REG_BITS_T_PTR pReg)
{
	uint8_t                cmd[2] = { 0 };
	uint16_t               w_cmd_num = 0;
	uint16_t               r_cmd_num = 0;
	uint8_t                buf_r[2] = { 0 };
	int32_t                ret = SENSOR_K_SUCCESS;
	struct i2c_msg         msg_r[2];
	uint16_t               reg_addr;
	int                    i;
	SENSOR_CHECK_ZERO(s_p_sensor_mod);

	reg_addr = pReg->reg_addr;

	if (SENSOR_I2C_REG_16BIT ==(pReg->reg_bits & SENSOR_I2C_REG_16BIT)) {
		cmd[w_cmd_num++] = (uint8_t) ((reg_addr >> 8) & SENSOR_LOW_EIGHT_BIT);
		cmd[w_cmd_num++] = (uint8_t) (reg_addr & SENSOR_LOW_EIGHT_BIT);
	} else {
		cmd[w_cmd_num++] = (uint8_t) reg_addr;
	}

	if (SENSOR_I2C_VAL_16BIT == (pReg->reg_bits & SENSOR_I2C_VAL_16BIT)) {
		r_cmd_num = SENSOR_CMD_BITS_16;
	} else {
		r_cmd_num = SENSOR_CMD_BITS_8;
	}

	for (i = 0; i < SENSOR_I2C_OP_TRY_NUM; i++) {
		msg_r[0].addr = s_p_sensor_mod->cur_i2c_client->addr;
		msg_r[0].flags = 0;
		msg_r[0].buf = cmd;
		msg_r[0].len = w_cmd_num;
		msg_r[1].addr = s_p_sensor_mod->cur_i2c_client->addr;
		msg_r[1].flags = I2C_M_RD;
		msg_r[1].buf = buf_r;
		msg_r[1].len = r_cmd_num;
		ret = i2c_transfer(s_p_sensor_mod->cur_i2c_client->adapter, msg_r, 2);
		if (ret != 2) {
			SENSOR_PRINT_ERR("SENSOR:read reg fail, ret %d, addr 0x%x, reg_addr 0x%x \n",
					ret, s_p_sensor_mod->cur_i2c_client->addr,reg_addr);
			SLEEP_MS(20);
			ret = SENSOR_K_FAIL;
		} else {
			pReg->reg_value = (r_cmd_num == 1) ? (uint16_t) buf_r[0] : (uint16_t) ((buf_r[0] << 8) + buf_r[1]);
			ret = SENSOR_K_SUCCESS;
			break;
		}
	}

	return ret;
}

LOCAL int _Sensor_K_WriteReg(SENSOR_REG_BITS_T_PTR pReg)
{
	uint8_t            cmd[4] = { 0 };
	uint32_t           index = 0;
	uint32_t           cmd_num = 0;
	struct i2c_msg     msg_w;
	int32_t            ret = SENSOR_K_SUCCESS;
	uint16_t           subaddr;
	uint16_t           data;
	int                i;
	SENSOR_CHECK_ZERO(s_p_sensor_mod);

	subaddr = pReg->reg_addr;
	data = pReg->reg_value;

	if (SENSOR_I2C_REG_16BIT ==(pReg->reg_bits & SENSOR_I2C_REG_16BIT)) {
		cmd[cmd_num++] = (uint8_t) ((subaddr >> 8) & SENSOR_LOW_EIGHT_BIT);
		index++;
		cmd[cmd_num++] =  (uint8_t) (subaddr & SENSOR_LOW_EIGHT_BIT);
		index++;
	} else {
		cmd[cmd_num++] = (uint8_t) subaddr;
		index++;
	}

	if (SENSOR_I2C_VAL_16BIT == (pReg->reg_bits & SENSOR_I2C_VAL_16BIT)) {
		cmd[cmd_num++] = (uint8_t) ((data >> 8) & SENSOR_LOW_EIGHT_BIT);
		index++;
		cmd[cmd_num++] = (uint8_t) (data & SENSOR_LOW_EIGHT_BIT);
		index++;
	} else {
		cmd[cmd_num++] = (uint8_t) data;
		index++;
	}

	if (SENSOR_WRITE_DELAY != subaddr) {
		for (i = 0; i < SENSOR_I2C_OP_TRY_NUM; i++) {
			msg_w.addr = s_p_sensor_mod->cur_i2c_client->addr;
			msg_w.flags = 0;
			msg_w.buf = cmd;
			msg_w.len = index;
			ret = i2c_transfer(s_p_sensor_mod->cur_i2c_client->adapter, &msg_w, 1);
			if (ret != 1) {
				SENSOR_PRINT_ERR("_Sensor_K_WriteReg failed:i2cAddr=%x, addr=%x, value=%x, bit=%d \n",
						s_p_sensor_mod->cur_i2c_client->addr, pReg->reg_addr, pReg->reg_value, pReg->reg_bits);
				ret = SENSOR_K_FAIL;
				continue;
			} else {
				ret = SENSOR_K_SUCCESS;
				break;
			}
		}
	} else {
		SLEEP_MS(data);
	}

	return ret;
}

LOCAL int _sensor_k_get_flash_level(SENSOR_FLASH_LEVEL_T *level)
{
	level->low_light  = SPRD_FLASH_LOW_CUR;
	level->high_light = SPRD_FLASH_HIGH_CUR;

	SENSOR_PRINT("Sensor get flash lvl: low %d, high %d \n", level->low_light, level->high_light);

	return SENSOR_K_SUCCESS;
}

int _sensor_burst_write_init(SENSOR_REG_T_PTR p_reg_table, uint32_t init_table_size);

LOCAL int _sensor_k_wr_regtab(SENSOR_REG_TAB_PTR pRegTab)
{
	char                   *pBuff = PNULL;
	uint32_t               cnt = pRegTab->reg_count;
	int                    ret = SENSOR_K_SUCCESS;
	uint32_t               size;
	SENSOR_REG_T_PTR       sensor_reg_ptr;
	SENSOR_REG_BITS_T      reg_bit;
	uint32_t               i;
	int                    rettmp;
	struct timeval         time1, time2;

	do_gettimeofday(&time1);

	size = cnt*sizeof(SENSOR_REG_T);
	pBuff = _sensor_k_kmalloc(size);
	if (PNULL == pBuff) {
		ret = SENSOR_K_FAIL;
		SENSOR_PRINT_ERR("sensor W RegTab err:alloc fail, cnt %d, size %d\n", cnt, size);
		goto _Sensor_K_WriteRegTab_return;
	} else {
		SENSOR_PRINT("sensor W RegTab: alloc success, cnt %d, size %d \n",cnt, size);
	}

	if (copy_from_user(pBuff, pRegTab->sensor_reg_tab_ptr, size)) {
		ret = SENSOR_K_FAIL;
		SENSOR_PRINT_ERR("sensor w err:copy user fail, size %d \n", size);
		goto _Sensor_K_WriteRegTab_return;
	}

	sensor_reg_ptr = (SENSOR_REG_T_PTR)pBuff;

	if (0 == pRegTab->burst_mode) {
		for (i=0; i<cnt; i++) {
			reg_bit.reg_addr  = sensor_reg_ptr[i].reg_addr;
			reg_bit.reg_value = sensor_reg_ptr[i].reg_value;
			reg_bit.reg_bits  = pRegTab->reg_bits;

			rettmp = _Sensor_K_WriteReg(&reg_bit);
			if(SENSOR_K_FAIL == rettmp)
				ret = SENSOR_K_FAIL;
		}
	} else if (SENSOR_I2C_BUST_NB == pRegTab->burst_mode) {
		printk("CAM %s, Line %d, burst_mode=%d, cnt=%d, start \n", __FUNCTION__, __LINE__, pRegTab->burst_mode, cnt);
		ret = _sensor_burst_write_init(sensor_reg_ptr, pRegTab->reg_count);
		printk("CAM %s, Line %d, burst_mode=%d, cnt=%d end\n", __FUNCTION__, __LINE__, pRegTab->burst_mode, cnt);
	}


_Sensor_K_WriteRegTab_return:
	if (PNULL != pBuff)
		_sensor_k_kfree(pBuff);

	do_gettimeofday(&time2);
	SENSOR_PRINT("sensor w RegTab: done, ret %d, cnt %d, time %d us \n", ret, cnt,
		(uint32_t)((time2.tv_sec - time1.tv_sec)*1000000+(time2.tv_usec - time1.tv_usec)));
	return ret;
}

LOCAL int _sensor_k_set_i2c_clk(uint32_t clock)
{
	sprd_i2c_ctl_chg_clk(SENSOR_I2C_ID, clock);
	SENSOR_PRINT("sensor set i2c clk %d  \n", clock);

	return SENSOR_K_SUCCESS;
}

LOCAL int _sensor_k_wr_i2c(SENSOR_I2C_T_PTR pI2cTab)
{
	char            *pBuff = PNULL;
	struct          i2c_msg msg_w;
	uint32_t        cnt = pI2cTab->i2c_count;
	int             ret = SENSOR_K_FAIL;
	SENSOR_CHECK_ZERO(s_p_sensor_mod);

	pBuff = _sensor_k_kmalloc(cnt);
	if (PNULL == pBuff) {
		SENSOR_PRINT_ERR("sensor W I2C ERR: alloc fail, size %d\n", cnt);
		goto sensor_k_writei2c_return;
	} else {
		SENSOR_PRINT("sensor W I2C: alloc success, size %d\n", cnt);
	}

	if (copy_from_user(pBuff, pI2cTab->i2c_data, cnt)) {
		SENSOR_PRINT_ERR("sensor W I2C ERR: copy user fail, size %d \n", cnt);
		goto sensor_k_writei2c_return;
	}

	msg_w.addr = pI2cTab->slave_addr;
	msg_w.flags = 0;
	msg_w.buf = pBuff;
	msg_w.len = cnt;

	ret = i2c_transfer(s_p_sensor_mod->cur_i2c_client->adapter, &msg_w, 1);
	if (ret != 1) {
		SENSOR_PRINT_ERR("SENSOR: w reg fail, ret: %d, addr: 0x%x\n",
		ret, msg_w.addr);
	} else {
		ret = SENSOR_K_SUCCESS;
	}

sensor_k_writei2c_return:
	if(PNULL != pBuff)
		_sensor_k_kfree(pBuff);

	SENSOR_PRINT("sensor w done, ret %d \n", ret);
	return ret;
}

LOCAL int _sensor_csi2_error(uint32_t err_id, uint32_t err_status, void* u_data)
{
	int                      ret = 0;

	printk("V4L2: csi2_error, %d 0x%x \n", err_id, err_status);

	return ret;

}

int sensor_k_open(struct inode *node, struct file *file)
{
	int	ret = 0;

	if(atomic_inc_return(&s_p_sensor_mod->open_count) == 1){
		struct miscdevice *md = file->private_data;
		struct device_node *dn = md->this_device->of_node;
		ret = clk_mm_i_eb(dn,1);
		s_p_sensor_mod->ccir_clk = parse_clk(dn, SENSOR_CLK);
		if (NULL == s_p_sensor_mod->ccir_clk) {
			SENSOR_PRINT_ERR("###: Failed: Can't get clock [ccir_mclk]!\n");
			SENSOR_PRINT_ERR("###: s_sensor_clk = %p.\n",s_p_sensor_mod->ccir_clk);
		} else {
			SENSOR_PRINT_ERR("###sensor ccir clk get ok.\n");
		}
	}
	return ret;
}

int sensor_k_release(struct inode *node, struct file *file)
{
	int	ret = 0;
		struct miscdevice *md = file->private_data;
		struct device_node *dn = md->this_device->of_node;

	if(atomic_dec_return(&s_p_sensor_mod->open_count) == 0){
		sensor_k_set_voltage_cammot(SENSOR_VDD_CLOSED);
		sensor_k_set_voltage_avdd(SENSOR_VDD_CLOSED);
#ifdef CONFIG_MACH_KIRAN
		sensor_k_set_voltage_dvdd(SENSOR_VDD_CLOSED, SENSOR_MAIN);
		sensor_k_set_voltage_dvdd(SENSOR_VDD_CLOSED, SENSOR_SUB);
#else
		sensor_k_set_voltage_dvdd(SENSOR_VDD_CLOSED);
#endif
		sensor_k_set_voltage_iovdd(SENSOR_VDD_CLOSED);
		sensor_k_set_mclk(0);
		clk_put(s_p_sensor_mod->ccir_clk);
		s_p_sensor_mod->ccir_clk = NULL;
		ret = clk_mm_i_eb(dn,0);
	}
	return ret;
}

LOCAL ssize_t sensor_k_read(struct file *filp, char __user *ubuf, size_t cnt, loff_t *gpos)
{
	return 0;
}

LOCAL ssize_t sensor_k_write(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *gpos)
{
	char           buf[64];
	char           *pBuff = PNULL;
	struct         i2c_msg msg_w;
	int            ret = SENSOR_K_FAIL;
	int            need_alloc = 1;
	SENSOR_CHECK_ZERO(s_p_sensor_mod);

	SENSOR_PRINT("sensor w cnt %d, buf %d\n", cnt, sizeof(buf));

	if (cnt < sizeof(buf)) {
		pBuff = buf;
		need_alloc = 0;
	}  else {
		pBuff = _sensor_k_kmalloc(cnt);
		if (PNULL == pBuff) {
			SENSOR_PRINT_ERR("sensor w ERR: alloc fail, size %d \n", cnt);
			goto sensor_k_write_return;
		} else {
			SENSOR_PRINT("sensor w: alloc success, size %d \n", cnt);
		}
	}

	if (copy_from_user(pBuff, ubuf, cnt)) {
		SENSOR_PRINT_ERR("sensor w ERR: copy user fail, size %d\n", cnt);
		goto sensor_k_write_return;
	}
	printk("sensor clnt addr 0x%x.\n", s_p_sensor_mod->cur_i2c_client->addr);
	msg_w.addr = s_p_sensor_mod->cur_i2c_client->addr;
	msg_w.flags = 0;
	msg_w.buf = pBuff;
	msg_w.len = cnt;

	ret = i2c_transfer(s_p_sensor_mod->cur_i2c_client->adapter, &msg_w, 1);
	if (ret != 1) {
		SENSOR_PRINT_ERR("SENSOR: w reg fail, ret %d, w addr: 0x%x,\n",
				ret, s_p_sensor_mod->cur_i2c_client->addr);
	} else {
		ret = SENSOR_K_SUCCESS;
	}

sensor_k_write_return:
	if ((PNULL != pBuff) && need_alloc)
		_sensor_k_kfree(pBuff);

	SENSOR_PRINT("sensor w done, ret %d \n", ret);
	return ret;
}

#if defined(CONFIG_MACH_YOUNG23GDTV) || defined(CONFIG_MACH_KIRAN)
#define BURST_MODE_BUFFER_MAX_SIZE 255
#define BURST_REG 0x0e
#define DELAY_REG 0xff

int _sensor_burst_write_init(SENSOR_REG_T_PTR p_reg_table, uint32_t init_table_size)
{
	int rtn = 0;
	int                   ret = 0;
	int					  idx = 0;
	uint32_t              i = 0;
	uint8_t burstmode_data[BURST_MODE_BUFFER_MAX_SIZE]={0};
	struct i2c_msg        msg;
	struct i2c_client     *i2c_client = PNULL;
	unsigned char buf[2] = { 0 };
	unsigned short subaddr = 0;
	unsigned short value = 0;
	int burst_flag = 0;
	int burst_cnt = 0;

	SENSOR_CHECK_ZERO(s_p_sensor_mod);

	i2c_client = s_p_sensor_mod->cur_i2c_client;
	if (0 == i2c_client) {
		SENSOR_PRINT_ERR("SENSOR: burst w Init err, i2c_clnt NULL!.\n");
		return -1;
	}


	msg.addr = i2c_client->addr;
	msg.flags = 0;

//    printk("%s: init_table_size %d\n", __FUNCTION__, init_table_size);
	for(i=0; i < init_table_size; i++)
	{

		if(idx > BURST_MODE_BUFFER_MAX_SIZE - 10){
			printk("Burst mode buffer overflow! Burst Count %d, idx=%d\n",  burst_cnt, idx);
		}
		subaddr = p_reg_table[i].reg_addr;
		value = p_reg_table[i].reg_value;

//        printk("B a:0x%x v:0x%x\n", subaddr, value);

		if(burst_flag == 0){
			switch(subaddr){
				case BURST_REG:
					if(value!=0x00){
							burst_flag = 1;
							burst_cnt++;
					}
					break;
				case DELAY_REG:
					if(value!=DELAY_REG)
						msleep(value*10);
					break;
				default:
					idx = 0;
					buf[0] = subaddr;
					buf[1] = value;
					msg.buf = buf;
					msg.len = 2;
					ret = i2c_transfer(i2c_client->adapter, &msg, 1);
					break;

			}
		}
		else if(burst_flag == 1)
		{
			if(subaddr == BURST_REG && value == 0x00){
				msg.len = idx;
				msg.buf = burstmode_data;
				ret = i2c_transfer(i2c_client->adapter, &msg, 1);

				if(ret != 1)
				{
					rtn = 1;
					break;
				}
				burst_flag = 0;
				idx = 0;
			}
			else{
				if(idx == 0){
					burstmode_data[idx++] = subaddr;
				}
				burstmode_data[idx++] = value;
			}

		}

	}

	return rtn;
}
#else
int _sensor_burst_write_init(SENSOR_REG_T_PTR p_reg_table, uint32_t init_table_size)
{
	uint32_t              rtn = 0;
	int                   ret = 0;
	uint32_t              i = 0;
	uint32_t              written_num = 0;
	uint16_t              wr_reg = 0;
	uint16_t              wr_val = 0;
	uint32_t              wr_num_once = 0;
	uint8_t               *p_reg_val_tmp = 0;
	struct i2c_msg        msg_w;
	struct i2c_client     *i2c_client = PNULL;

	SENSOR_CHECK_ZERO(s_p_sensor_mod);

	i2c_client = s_p_sensor_mod->cur_i2c_client;

	printk("SENSOR: burst w Init\n");
	if (0 == i2c_client) {
		SENSOR_PRINT_ERR("SENSOR: burst w Init err, i2c_clnt NULL!.\n");
		return -1;
	}
	p_reg_val_tmp = (uint8_t*)_sensor_k_kzalloc(init_table_size*sizeof(uint16_t) + 16);

	if(PNULL == p_reg_val_tmp){
		SENSOR_PRINT_ERR("_sensor_burst_write_init ERROR: alloc is fail, size = %d \n", init_table_size*sizeof(uint16_t) + 16);
		return -1;
	}
	else{
		SENSOR_PRINT_HIGH("_sensor_burst_write_init: alloc success, size = %d \n", init_table_size*sizeof(uint16_t) + 16);
	}


	while (written_num < init_table_size) {
		wr_num_once = 2;

		wr_reg = p_reg_table[written_num].reg_addr;
		wr_val = p_reg_table[written_num].reg_value;
		if (SENSOR_WRITE_DELAY == wr_reg) {
			if (wr_val >= 10) {
				msleep(wr_val);
			} else {
				mdelay(wr_val);
			}
		} else {
			p_reg_val_tmp[0] = (uint8_t)(wr_reg);
			p_reg_val_tmp[1] = (uint8_t)(wr_val);

			if ((0x0e == wr_reg) && (0x01 == wr_val)) {
				for (i = written_num + 1; i < init_table_size; i++) {
					if ((0x0e == wr_reg) && (0x00 == wr_val)) {
						break;
					} else {
						wr_val = p_reg_table[i].reg_value;
						p_reg_val_tmp[wr_num_once+1] = (uint8_t)(wr_val);
						wr_num_once ++;
					}
				}
			}
			msg_w.addr = i2c_client->addr;
			msg_w.flags = 0;
			msg_w.buf = p_reg_val_tmp;
			msg_w.len = (uint32_t)(wr_num_once);
			ret = i2c_transfer(i2c_client->adapter, &msg_w, 1);
			if (ret!=1) {
				SENSOR_PRINT("SENSOR: s err, val {0x%x 0x%x} {0x%x 0x%x} {0x%x 0x%x} {0x%x 0x%x} {0x%x 0x%x} {0x%x 0x%x}.\n",
					p_reg_val_tmp[0],p_reg_val_tmp[1],p_reg_val_tmp[2],p_reg_val_tmp[3],
					p_reg_val_tmp[4],p_reg_val_tmp[5],p_reg_val_tmp[6],p_reg_val_tmp[7],
					p_reg_val_tmp[8],p_reg_val_tmp[9],p_reg_val_tmp[10],p_reg_val_tmp[11]);
					SENSOR_PRINT("SENSOR: i2c w once err\n");
				rtn = 1;
				break;
			}
		}
		written_num += wr_num_once - 1;
	}
	SENSOR_PRINT("SENSOR: burst w Init OK\n");
	_sensor_k_kfree(p_reg_val_tmp);
	return rtn;
}
#endif

LOCAL long sensor_k_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	long ret = 0;
	SENSOR_CHECK_ZERO(s_p_sensor_mod);

	mutex_lock(&s_p_sensor_mod->sensor_lock);

	switch (cmd) {
	case SENSOR_IO_PD:
		{
			BOOLEAN power_level;
			ret = copy_from_user(&power_level, (BOOLEAN *) arg, sizeof(BOOLEAN));

			if (0 == ret)
				ret = sensor_k_set_pd_level(power_level);
		}
		break;

	case SENSOR_IO_SET_CAMMOT:
		{
			uint32_t vdd_val;
			ret = copy_from_user(&vdd_val, (uint32_t *) arg, sizeof(uint32_t));
			if (0 == ret)
				ret = sensor_k_set_voltage_cammot(vdd_val);
		}
		break;

	case SENSOR_IO_SET_AVDD:
		{
			uint32_t vdd_val;
			ret = copy_from_user(&vdd_val, (uint32_t *) arg, sizeof(uint32_t));
			if (0 == ret)
				ret = sensor_k_set_voltage_avdd(vdd_val);
		}
		break;
#ifdef CONFIG_MACH_KIRAN
	case SENSOR_IO_SET_DVDD:
		{
			uint32_t param[2];
			ret = copy_from_user(&param, (uint32_t *) arg, 2*sizeof(uint32_t));
			if (0 == ret)
				ret = sensor_k_set_voltage_dvdd(param[0], param[1]);
		}
		break;
#else
	case SENSOR_IO_SET_DVDD:
		{
			uint32_t vdd_val;
			ret = copy_from_user(&vdd_val, (uint32_t *) arg, sizeof(uint32_t));
			if (0 == ret)
				ret = sensor_k_set_voltage_dvdd(vdd_val);
		}
		break;
#endif
	case SENSOR_IO_SET_IOVDD:
		{
			uint32_t vdd_val;
			ret = copy_from_user(&vdd_val, (uint32_t *) arg, sizeof(uint32_t));
			if (0 == ret)
				ret = sensor_k_set_voltage_iovdd(vdd_val);
		}
		break;

	case SENSOR_IO_SET_MCLK:
		{
			uint32_t mclk;
			ret = copy_from_user(&mclk, (uint32_t *) arg, sizeof(uint32_t));
			if (0 == ret)
				ret = sensor_k_set_mclk(mclk);
		}
		break;

	case SENSOR_IO_RST:
		{
			uint32_t rst_val[2];
			ret = copy_from_user(rst_val, (uint32_t *) arg, 2*sizeof(uint32_t));
			if (0 == ret)
				ret = _sensor_k_reset(rst_val[0], rst_val[1]);
		}
		break;

	case SENSOR_IO_I2C_INIT:
		{
			uint32_t sensor_id;
			ret = copy_from_user(&sensor_id, (uint32_t *) arg, sizeof(uint32_t));
			if (0 == ret)
				ret = sensor_k_sensor_sel(sensor_id);
		}
		break;

	case SENSOR_IO_I2C_DEINIT:
		{
			uint32_t sensor_id;
			ret = copy_from_user(&sensor_id, (uint32_t *) arg, sizeof(uint32_t));
			if (0 == ret)
				ret = sensor_k_sensor_desel(sensor_id);
		}
		break;

	case SENSOR_IO_SET_ID:
		{
			ret = copy_from_user(&s_p_sensor_mod->sensor_id, (uint32_t *) arg, sizeof(uint32_t));
		}
		break;

	case SENSOR_IO_RST_LEVEL:
		{
			uint32_t level;
			ret = copy_from_user(&level, (uint32_t *) arg, sizeof(uint32_t));
			if (0 == ret)
				ret = sensor_k_set_rst_level(level);
		}
		break;

	case SENSOR_IO_I2C_ADDR:
		{
			uint16_t i2c_addr;
			ret = copy_from_user(&i2c_addr, (uint16_t *) arg, sizeof(uint16_t));
			if (0 == ret) {
				s_p_sensor_mod->cur_i2c_client->addr = (s_p_sensor_mod->cur_i2c_client->addr & (~0xFF)) |i2c_addr;
				printk("SENSOR_IO_I2C_ADDR: addr = %x, %x \n", i2c_addr, s_p_sensor_mod->cur_i2c_client->addr);
			}
		}
		break;

	case SENSOR_IO_I2C_READ:
		{
			SENSOR_REG_BITS_T reg;
			ret = copy_from_user(&reg, (SENSOR_REG_BITS_T *) arg, sizeof(SENSOR_REG_BITS_T));

			if (0 == ret) {
				ret = _Sensor_K_ReadReg(&reg);
				if(SENSOR_K_FAIL != ret){
					ret = copy_to_user((SENSOR_REG_BITS_T *)arg, &reg, sizeof(SENSOR_REG_BITS_T));
				}
			}
		}
		break;

	case SENSOR_IO_I2C_WRITE:
		{
			SENSOR_REG_BITS_T reg;
			ret = copy_from_user(&reg, (SENSOR_REG_BITS_T *) arg, sizeof(SENSOR_REG_BITS_T));

			if (0 == ret) {
				ret = _Sensor_K_WriteReg(&reg);
			}

		}
		break;

	case SENSOR_IO_I2C_WRITE_REGS:
		{
			SENSOR_REG_TAB_T regTab;
			ret = copy_from_user(&regTab, (SENSOR_REG_TAB_T *) arg, sizeof(SENSOR_REG_TAB_T));
			if (0 == ret)
				ret = _sensor_k_wr_regtab(&regTab);
		}
		break;

	case SENSOR_IO_SET_I2CCLOCK:
		{
			uint32_t clock;
			ret = copy_from_user(&clock, (uint32_t *) arg, sizeof(uint32_t));
			if(0 == ret){
				_sensor_k_set_i2c_clk(clock);
			}
		}
		break;

	case SENSOR_IO_I2C_WRITE_EXT:
		{
			SENSOR_I2C_T i2cTab;
			ret = copy_from_user(&i2cTab, (SENSOR_I2C_T *) arg, sizeof(SENSOR_I2C_T));
			if (0 == ret)
				ret = _sensor_k_wr_i2c(&i2cTab);
		}
		break;

	case SENSOR_IO_GET_FLASH_LEVEL:
		{
			SENSOR_FLASH_LEVEL_T flash_level;
			ret = copy_from_user(&flash_level, (SENSOR_FLASH_LEVEL_T *) arg, sizeof(SENSOR_FLASH_LEVEL_T));
			if (0 == ret) {
				ret = _sensor_k_get_flash_level(&flash_level);
				if(SENSOR_K_FAIL != ret){
					ret = copy_to_user((SENSOR_FLASH_LEVEL_T *)arg, &flash_level, sizeof(SENSOR_FLASH_LEVEL_T));
				}
			}
		}
		break;
	case SENSOR_IO_GET_SOCID:
		{
			SENSOR_SOCID_T  Id  ;
			Id.d_die=sci_get_chip_id();
			Id.a_die=sci_get_ana_chip_id()|sci_get_ana_chip_ver();
			SENSOR_PRINT("cpu id 0x%x,0x%x  \n", Id.d_die,Id.a_die);
			ret = copy_to_user((SENSOR_SOCID_T *)arg, &Id, sizeof(SENSOR_SOCID_T));
		}
		break;
	case SENSOR_IO_IF_CFG:
		{
			SENSOR_IF_CFG_T if_cfg;
			ret = copy_from_user((void*)&if_cfg, (SENSOR_IF_CFG_T *)arg, sizeof(SENSOR_IF_CFG_T));
			if (0 == ret) {
				if (INTERFACE_OPEN == if_cfg.is_open) {
					if (INTERFACE_MIPI == if_cfg.if_type) {
						if (0 == s_p_sensor_mod->mipi_on) {
							struct miscdevice *md = file->private_data ;
							_sensor_k_mipi_clk_en(md->this_device->of_node);
							udelay(1);
							csi_api_init(if_cfg.bps_per_lane, if_cfg.phy_id);
							csi_api_start();
							csi_reg_isr(_sensor_csi2_error, (void*)s_p_sensor_mod);
							csi_set_on_lanes(if_cfg.lane_num);
							s_p_sensor_mod->mipi_on = 1;
							printk("MIPI on, lane %d, bps %d, wait 10us \n", if_cfg.lane_num, if_cfg.bps_per_lane);
						} else {
							printk("MIPI already on \n");
						}
					}
				} else {
					if (INTERFACE_MIPI == if_cfg.if_type) {
						if (1 == s_p_sensor_mod->mipi_on) {
							csi_api_close(if_cfg.phy_id);
							_sensor_k_mipi_clk_dis();
							s_p_sensor_mod->mipi_on = 0;
							printk("MIPI off \n");
						} else {
							printk("MIPI already off \n");
						}

					}

				}
			}
		}
		break;
	case SENSOR_IO_POWER_CFG:
		{
			SENSOR_POWER_CFG_T pwr_cfg;

			ret = copy_from_user(&pwr_cfg, (SENSOR_POWER_CFG_T*) arg, sizeof(SENSOR_POWER_CFG_T));
			if (0 == ret) {
				if (pwr_cfg.is_on) {
					ret = sensor_power_on(pwr_cfg.op_sensor_id, &pwr_cfg.main_sensor, &pwr_cfg.sub_sensor);
				} else {
					ret = sensor_power_off(pwr_cfg.op_sensor_id, &pwr_cfg.main_sensor, &pwr_cfg.sub_sensor);
				}
			}
		}
		break;
	default:
		SENSOR_PRINT("sensor_k_ioctl: inv cmd %x  \n", cmd);
		break;
	}

	mutex_unlock(&s_p_sensor_mod->sensor_lock);
	return (long)ret;
}


LOCAL struct file_operations sensor_fops = {
	.owner = THIS_MODULE,
	.open = sensor_k_open,
	.read = sensor_k_read,
	.write = sensor_k_write,
	.unlocked_ioctl = sensor_k_ioctl,
	.release = sensor_k_release,
};

LOCAL struct miscdevice sensor_dev = {
	.minor = SENSOR_MINOR,
	.name = "sprd_sensor",
	.fops = &sensor_fops,
};

int sensor_k_probe(struct platform_device *pdev)
{
	int              ret = 0;
	uint32_t         tmp = 0;
	SENSOR_CHECK_ZERO(s_p_sensor_mod);

	printk(KERN_ALERT "sensor probe called\n");

	ret = misc_register(&sensor_dev);
	if (ret) {
		printk(KERN_ERR "can't reg miscdev on minor=%d (%d)\n",
			SENSOR_MINOR, ret);
		return ret;
	}
#ifdef CONFIG_OF
	sensor_dev.this_device->of_node = pdev->dev.of_node;
	s_p_sensor_mod->pin_main_reset = of_get_gpio(sensor_dev.this_device->of_node,0);
	s_p_sensor_mod->pin_main_pd = of_get_gpio(sensor_dev.this_device->of_node,1);
	s_p_sensor_mod->pin_sub_reset = of_get_gpio(sensor_dev.this_device->of_node,2);
	s_p_sensor_mod->pin_sub_pd = of_get_gpio(sensor_dev.this_device->of_node,3);
#else
	s_p_sensor_mod->pin_main_reset = GPIO_SENSOR_RESET;
	s_p_sensor_mod->pin_main_pd= GPIO_MAIN_SENSOR_PWN;
	s_p_sensor_mod->pin_sub_reset = GPIO_SUB_SENSOR_RESET;
	s_p_sensor_mod->pin_sub_pd= GPIO_SUB_SENSOR_PWN;
#endif
	printk("sensor pin_main_reset =%d\n",s_p_sensor_mod->pin_main_reset);
	printk("sensor pin_main_pd =%d\n",s_p_sensor_mod->pin_main_pd);
	printk("sensor pin_sub_reset =%d\n",s_p_sensor_mod->pin_sub_reset);
	printk("sensor pin_sub_pd =%d\n",s_p_sensor_mod->pin_sub_pd);

	ret = gpio_request(s_p_sensor_mod->pin_main_reset, "main camera rst");
        if (ret) {
		tmp = s_p_sensor_mod->pin_main_reset;
            goto gpio_err_exit;
        }

	ret = gpio_request(s_p_sensor_mod->pin_main_pd, "main camera pd");
	if (ret) {
		tmp = s_p_sensor_mod->pin_main_pd;
		goto gpio_err_exit;
	}

	if (s_p_sensor_mod->pin_sub_reset != s_p_sensor_mod->pin_main_reset) {
		ret = gpio_request(s_p_sensor_mod->pin_sub_reset, "sub camera rst");
	if (ret) {
			tmp = s_p_sensor_mod->pin_sub_reset;
		goto gpio_err_exit;
	}
	}

	if (s_p_sensor_mod->pin_sub_pd != s_p_sensor_mod->pin_main_pd) {
		ret = gpio_request(s_p_sensor_mod->pin_sub_pd, "sub camera pd");
	if (ret) {
			tmp = s_p_sensor_mod->pin_sub_pd;
		goto gpio_err_exit;
	}
	}

	gpio_direction_output(s_p_sensor_mod->pin_main_reset, 1);

	s_p_sensor_mod->sensor_i2c_driver.driver.owner = THIS_MODULE;
	s_p_sensor_mod->sensor_i2c_driver.probe  = sensor_probe;
	s_p_sensor_mod->sensor_i2c_driver.remove = sensor_remove;
	s_p_sensor_mod->sensor_i2c_driver.detect = sensor_detect;
	s_p_sensor_mod->sensor_i2c_driver.driver.name = SENSOR_MAIN_I2C_NAME;
	s_p_sensor_mod->sensor_i2c_driver.id_table = c_sensor_device_id;
	s_p_sensor_mod->sensor_i2c_driver.address_list = &c_sensor_main_default_addr_list[0];

	ret = i2c_add_driver(&s_p_sensor_mod->sensor_i2c_driver);
	if (ret) {
		SENSOR_PRINT_ERR("+I2C err %d.\n", ret);
		return SENSOR_K_FAIL;
	} else {
		SENSOR_PRINT_HIGH("+I2C OK \n");
	}

gpio_err_exit:
	if (ret) {
		printk(KERN_ERR "sensor prb fail req gpio %d err %d\n",
			tmp, ret);
	} else {
		printk(KERN_ALERT " sensor prb Success\n");
	}
	return ret;
}

LOCAL int sensor_k_remove(struct platform_device *dev)
{
	printk(KERN_INFO "sensor remove called !\n");

	if (s_p_sensor_mod->pin_sub_reset != s_p_sensor_mod->pin_main_reset) {
		gpio_free(s_p_sensor_mod->pin_sub_reset);
	}
	if (s_p_sensor_mod->pin_sub_pd != s_p_sensor_mod->pin_main_pd) {
		gpio_free(s_p_sensor_mod->pin_sub_pd);
	}
	gpio_free(s_p_sensor_mod->pin_main_reset);
	gpio_free(s_p_sensor_mod->pin_main_pd);

	misc_deregister(&sensor_dev);
	printk(KERN_INFO "sensor remove Success !\n");
	return 0;
}

LOCAL const struct of_device_id of_match_table_sensor[] = {
	{ .compatible = "sprd,sprd_sensor", },
	{ },
};

static struct platform_driver sensor_dev_driver = {
	.probe = sensor_k_probe,
	.remove =sensor_k_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "sprd_sensor",
		.of_match_table = of_match_ptr(of_match_table_sensor),
		},
};

#if defined(CONFIG_MACH_CORE3_W) || defined(CONFIG_MACH_YOUNG23GDTV) || defined(CONFIG_MACH_KIRAN)

struct class *camera_class;

#if defined(CONFIG_MACH_YOUNG23GDTV)
#define REAR_SENSOR_NAME	"SR352\n"
#elif defined(CONFIG_MACH_KIRAN)
#define REAR_SENSOR_NAME	"SR352\n"
#define FRONT_SENSOR_NAME	"SR030PC50\n"
#else
#define REAR_SENSOR_NAME	"SR544\n"
#define FRONT_SENSOR_NAME	"SR200PC20\n"
#endif
#define SENSOR_TYPE	"SOC\n"

#if defined(CONFIG_FLED_SM5701)
LOCAL int _Sensor_K_SetTorch(uint32_t flash_mode)
{
	printk("_Sensor_K_SetTorch mode %d	flash_torch_status =%d\n", flash_mode,flash_torch_status);
	switch (flash_mode) {
	case 1:        /*for torch */
		flash_torch_status=1;
	    sm5701_led_ready(MOVIE_MODE);
        sm5701_set_fleden(SM5701_FLEDEN_ON_MOVIE);
		break;
	case 0:
		flash_torch_status=0;
        sm5701_set_fleden(SM5701_FLEDEN_DISABLED);
        sm5701_led_ready(LED_DISABLE);
		break;
	default:
		printk("_Sensor_K_SetTorch unknow mode:flash_mode 0x%x \n", flash_mode);
		break;
	}
	printk("_Sensor_K_SetTorch: flash_mode 0x%x  \n", flash_mode);
	return 0;
}
#endif


static ssize_t Rear_Cam_Sensor_ID(struct device *dev, struct device_attribute *attr, char *buf)
{
	SENSOR_PRINT("Rear_Cam_Sensor_ID\n");
	return  sprintf(buf, REAR_SENSOR_NAME);
}

static ssize_t Cam_Sensor_TYPE(struct device *dev, struct device_attribute *attr, char *buf)
{
	SENSOR_PRINT("Cam_Sensor_type\n");
	return  sprintf(buf, SENSOR_TYPE);
}

static ssize_t Rear_Cam_FW_Store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{

	 SENSOR_PRINT("Rear_Cam_FW_Store value\n");
	 return 0;
}

static ssize_t Rear_Cam_Type_Store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{

	 SENSOR_PRINT("Rear_Cam_Type_Store value \n");
	 return 0;
}

#if defined(CONFIG_FLED_SM5701)
static ssize_t Rear_Cam_store_flash(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	 int value;
	 sscanf(buf, "%d", &value);

	 printk("Rear_Cam_store_flash value = %d\n", value);
     _Sensor_K_SetTorch(value);

	 return size;
}

static ssize_t Rear_Cam_show_flash(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	 SENSOR_PRINT("Rear_Cam_show_flash value\n");
	 return sprintf(buf,"%d",flash_torch_status);
}
#endif

static ssize_t Front_Cam_Sensor_ID(struct device *dev, struct device_attribute *attr, char *buf)
{
	SENSOR_PRINT("Front_Cam_Sensor_ID\n");
	return  sprintf(buf, FRONT_SENSOR_NAME);
}

static ssize_t Front_Cam_FW_Store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{

	 SENSOR_PRINT("Front_Cam_FW_Store value\n");
	 return 0;
}

static ssize_t Front_Cam_Type_Store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{

	 SENSOR_PRINT("Front_Cam_Type_Store value \n");
	 return 0;
}

static DEVICE_ATTR(rear_camfw, S_IRUGO | S_IWUSR, Rear_Cam_Sensor_ID, Rear_Cam_FW_Store);
static DEVICE_ATTR(rear_type, S_IRUGO | S_IWUSR , Cam_Sensor_TYPE, Rear_Cam_Type_Store);
#if defined(CONFIG_FLED_SM5701)
static DEVICE_ATTR(rear_flash, 0664, Rear_Cam_show_flash, Rear_Cam_store_flash);
#endif
static DEVICE_ATTR(front_camfw, S_IRUGO | S_IWUSR, Front_Cam_Sensor_ID, Front_Cam_FW_Store);
static DEVICE_ATTR(front_type, S_IRUGO | S_IWUSR , Cam_Sensor_TYPE, Front_Cam_Type_Store);
#endif

#ifdef CAM_AHB_CLOCK_MCU_SLEEP_FEATURE

static int scxx30_Cam_sensor_cpuidle_notify(struct notifier_block *nb, unsigned long event, void *dummy)
{
	if (event == SC_CPUIDLE_PREPARE){
		 if(s_p_sensor_mod->open_count.counter)
		{
			return NOTIFY_BAD;  //work now, can't not sleep
		}
		else
		{
			return NOTIFY_OK;
		}
	}
	else{
		printk("Cam_sensor: error in cpuidle notify type!\n");
	}
	return 0;

}

static struct notifier_block scxx30_Cam_sensor_cpuidle_notifier = {
	.notifier_call = scxx30_Cam_sensor_cpuidle_notify,
};
#endif


int __init sensor_k_init(void)
{
	printk(KERN_INFO "sensor_k_init called !\n");
	int err = 0;

	s_p_sensor_mod = (struct sensor_module *)vzalloc(sizeof(struct sensor_module));
	SENSOR_CHECK_ZERO(s_p_sensor_mod);

	s_p_sensor_mod->sensor_id = SENSOR_ID_MAX;
	mutex_init(&s_p_sensor_mod->sensor_lock);

	if (platform_driver_register(&sensor_dev_driver) != 0) {
		printk("platform device register Failed \n");
		return SENSOR_K_FAIL;
	}

#if defined(CONFIG_MACH_CORE3_W) || defined(CONFIG_MACH_YOUNG23GDTV) || defined(CONFIG_MACH_KIRAN)
	flash_torch_status=0;
	struct device *dev_t_rear;
	struct device *dev_t_front;

	SENSOR_PRINT("Start create camera related nodes\n");

	camera_class = class_create(THIS_MODULE, "camera");
	if (IS_ERR(camera_class))
	{
		SENSOR_PRINT("Failed to create camera_class!\n");
		platform_driver_unregister(&sensor_dev_driver);
		return PTR_ERR( camera_class );
	}

	dev_t_rear = device_create(camera_class, NULL, 0, "%s", "rear");
	if (IS_ERR(dev_t_rear))
	{
		platform_driver_unregister(&sensor_dev_driver);
		class_destroy(camera_class);
		SENSOR_PRINT("Failed to create camera_dev!\n");
		return PTR_ERR( dev_t_rear );
	}

	err =device_create_file(dev_t_rear, &dev_attr_rear_camfw);
	if(err)
	{
		SENSOR_PRINT("Failed to create device file(%s)!\n", dev_attr_rear_camfw.attr.name);
		goto err_make_rear_camfw_file;
	}

	err = device_create_file(dev_t_rear, &dev_attr_rear_type);
	if(err)
	{
		SENSOR_PRINT("Failed to create device file(%s)!\n", dev_attr_rear_type.attr.name);
		goto err_make_rear_type_file;
	}

#if defined(CONFIG_FLED_SM5701)
	err = device_create_file(dev_t_rear, &dev_attr_rear_flash);
	if(err)
	{
		SENSOR_PRINT("Failed to create device file(%s)!\n", dev_attr_rear_flash.attr.name);
		goto err_make_rear_flash_file;
	}
#endif
	dev_t_front = device_create(camera_class, NULL, 0, "%s", "front");
	if (IS_ERR(dev_t_front))
	{
		SENSOR_PRINT("Failed to create camera_dev front!\n");
		err = PTR_ERR( dev_t_front );
		goto err_make_front_device;
	}

	err = device_create_file(dev_t_front, &dev_attr_front_camfw);
	if(err)
	{
		SENSOR_PRINT("Failed to create device file(%s)!\n", dev_attr_front_camfw.attr.name);
		goto err_make_front_camfw_file;
	}

	err = device_create_file(dev_t_front, &dev_attr_front_type);
	if(err)
	{
		SENSOR_PRINT("Failed to create device file(%s)!\n", dev_attr_front_type.attr.name);
		goto err_make_front_type_file;
	}

#ifdef CAM_AHB_CLOCK_MCU_SLEEP_FEATURE
	err = register_sc_cpuidle_notifier(&scxx30_Cam_sensor_cpuidle_notifier);
	if (err) {
		printk("Cam_sensor: Failed to setup light sleep notifier!\n");
	}
#endif
	return 0;

	err_make_front_type_file:
		device_remove_file(dev_t_front, &dev_attr_front_camfw);
	err_make_front_camfw_file:
		device_destroy(camera_class, dev_t_front);
	err_make_front_device:
#if defined(CONFIG_FLED_SM5701)
		device_remove_file(dev_t_rear, &dev_attr_rear_flash);
	err_make_rear_flash_file:
#endif
		device_remove_file(dev_t_rear, &dev_attr_rear_type);
	err_make_rear_type_file:
		device_remove_file(dev_t_rear, &dev_attr_rear_camfw);
	err_make_rear_camfw_file:
		vfree(s_p_sensor_mod);
		s_p_sensor_mod=NULL;
		device_destroy(camera_class,dev_t_rear);
		class_destroy(camera_class);
		platform_driver_unregister(&sensor_dev_driver);
#endif
	return err;
}

void sensor_k_exit(void)
{
	printk(KERN_INFO "sensor_k_exit called !\n");
	platform_driver_unregister(&sensor_dev_driver);

	if (SENSOR_ADDR_INVALID(s_p_sensor_mod)) {
		printk("SENSOR: Invalid addr, 0x%x", (uint32_t)s_p_sensor_mod);
	} else {
		vfree(s_p_sensor_mod);
		s_p_sensor_mod = NULL;
	}
}

module_init(sensor_k_init);
module_exit(sensor_k_exit);

MODULE_DESCRIPTION("Sensor Driver");
MODULE_LICENSE("GPL");

