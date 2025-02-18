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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <asm/io.h>
#include <asm/setup.h>
#include <asm/mach/time.h>
#include <asm/mach/arch.h>
#include <asm/mach-types.h>
#include <linux/irqchip/arm-gic.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/localtimer.h>
#include <linux/debugfs.h>

#include <mach/hardware.h>
#include <linux/i2c.h>
#if(defined(CONFIG_INPUT_LIS3DH_ZT_I2C)||defined(CONFIG_INPUT_LIS3DH_I2C_MODULE))
#include <linux/i2c/lis3dh_zt.h>
#endif
#if(defined(CONFIG_INPUT_LTR558_I2C)||defined(CONFIG_INPUT_LTR558_I2C_MODULE))
#include <linux/i2c/ltr_558als.h>
#endif
//cg add
#if(defined(CONFIG_TOUCHSCREEN_MSG2138)||defined(CONFIG_TOUCHSCREEN_MSG2138_MODULE))
#include <linux/i2c/msg2138.h>
#endif
//cg

#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <mach/board.h>
#include <mach/serial_sprd.h>
#include <mach/adi.h>
#include <mach/adc.h>
#include <mach/pinmap.h>
#if(defined(CONFIG_INV_MPU_IIO)||defined(CONFIG_INV_MPU_IIO_MODULE))
#include <linux/mpu.h>
#endif
#if(defined(CONFIG_SENSORS_AK8975)||defined(CONFIG_SENSORS_AK8975_MODULE))
#include <linux/akm8975.h>
#endif
#if(defined(CONFIG_SENSORS_AK8963)||defined(CONFIG_SENSORS_AK8963_MODULE))
#include <linux/akm8963.h>
#endif
#include <linux/irq.h>
#include <linux/input/matrix_keypad.h>

#include <mach/sci.h>
#include <mach/sci_glb_regs.h>
#include <mach/hardware.h>
#include <mach/kpd.h>

#include "devices.h"

#include <linux/regulator/consumer.h>
#include <mach/regulator.h>
#if(defined(CONFIG_TOUCHSCREEN_FOCALTECH)||defined(CONFIG_TOUCHSCREEN_FOCALTECH_MODULE))
#include <linux/i2c/focaltech.h>
#endif
#include <mach/i2s.h>
#include <linux/sprd_2351.h>


extern void __init sci_reserve(void);
extern void __init sci_map_io(void);
extern void __init sci_init_irq(void);
extern void __init sci_timer_init(void);
extern int __init sci_clock_init(void);
extern int __init sci_regulator_init(void);
#ifdef CONFIG_ANDROID_RAM_CONSOLE
extern int __init sprd_ramconsole_init(void);
#endif

/*keypad define */
#define CUSTOM_KEYPAD_ROWS          (SCI_ROW0 | SCI_ROW1)
#define CUSTOM_KEYPAD_COLS          (SCI_COL0 | SCI_COL1)
#define ROWS	(2)
#define COLS	(2)

static const unsigned int board_keymap[] = {
	KEY(0, 0, KEY_VOLUMEDOWN),
	KEY(1, 0, KEY_VOLUMEUP),
	KEY(0, 1, KEY_HOME),
};

static const struct matrix_keymap_data customize_keymap = {
	.keymap = board_keymap,
	.keymap_size = ARRAY_SIZE(board_keymap),
};

static struct sci_keypad_platform_data sci_keypad_data = {
	.rows_choose_hw = CUSTOM_KEYPAD_ROWS,
	.cols_choose_hw = CUSTOM_KEYPAD_COLS,
	.rows_number = ROWS,
	.cols_number = COLS,
	.keymap_data = &customize_keymap,
	.support_long_key = 1,
	.repeat = 0,
	.debounce_time = 5000,
};

static struct platform_device rfkill_device;
static struct platform_device brcm_bluesleep_device;
static struct platform_device kb_backlight_device;

static struct platform_device *devices[] __initdata = {
	&sprd_serial_device0,
	&sprd_serial_device1,
	&sprd_serial_device2,
	&sprd_device_rtc,
	&sprd_eic_gpio_device,
	&sprd_nand_device,
	&sprd_lcd_device0,
#ifdef CONFIG_PSTORE_RAM
	&sprd_ramoops_device,
#endif
        &sprd_backlight_device,
	&sprd_i2c_device0,
	&sprd_i2c_device1,
	&sprd_i2c_device2,
	&sprd_i2c_device3,
	&sprd_spi0_device,
	&sprd_spi1_device,
	&sprd_spi2_device,
	&sprd_keypad_device,
	&sprd_battery_device,
#ifdef CONFIG_MUX_SDIO_OPT1_HAL
	&ipc_sdio_device,
#endif
	&sprd_emmc_device,
	&sprd_sdio0_device,
	&sprd_sdio1_device,
	&sprd_sdio2_device,
#ifdef CONFIG_ION
	&sprd_ion_dev,
#endif
	&sprd_vsp_device,
	&sprd_jpg_device,
	&sprd_dcam_device,
	&sprd_scale_device,
	&sprd_gsp_device,
	&sprd_rotation_device,
	&sprd_sensor_device,
	&sprd_isp_device,
	&sprd_dma_copy_device,
#if 0
	&sprd_ahb_bm0_device,
	&sprd_ahb_bm1_device,
	&sprd_ahb_bm2_device,
	&sprd_ahb_bm3_device,
	&sprd_ahb_bm4_device,
	&sprd_axi_bm0_device,
	&sprd_axi_bm1_device,
	&sprd_axi_bm2_device,
#endif
#ifdef CONFIG_BT_BCM4330
	&rfkill_device,
	&brcm_bluesleep_device,
#endif
	&kb_backlight_device,
	&sprd_a7_pmu_device,
#ifdef CONFIG_SIPC_TD
	&sprd_cproc_td_device,
	&sprd_spipe_td_device,
	&sprd_slog_td_device,
	&sprd_stty_td_device,
	&sprd_seth0_td_device,
	&sprd_seth1_td_device,
	&sprd_seth2_td_device,
	&sprd_saudio_td_device,
#ifdef CONFIG_SIPC_SPOOL
	&sprd_spool_td_device,
#endif
#endif
#ifdef CONFIG_SIPC_WCDMA
	&sprd_cproc_wcdma_device,
	&sprd_spipe_wcdma_device,
	&sprd_slog_wcdma_device,
	&sprd_stty_wcdma_device,
	&sprd_seth0_wcdma_device,
	&sprd_seth1_wcdma_device,
	&sprd_seth2_wcdma_device,
	&sprd_saudio_wcdma_device,
#ifdef CONFIG_SIPC_SPOOL
	&sprd_spool_wcdma_device,
#endif
#endif
#ifdef CONFIG_SIPC_WCN	
	&sprd_cproc_wcn_device,
	&sprd_spipe_wcn_device,
	&sprd_slog_wcn_device,
	&sprd_sttybt_td_device,
#endif
	&sprd_saudio_voip_device,
	&sprd_headset_device,
#ifdef CONFIG_RF_SHARK
	&trout_fm_device,
#endif

#ifdef CONFIG_SPRD_VETH
#ifdef CONFIG_MUX_SPI_HAL
        &sprd_veth_spi0_device,
        &sprd_veth_spi1_device,
        &sprd_veth_spi2_device,
        &sprd_veth_spi3_device,
        &sprd_veth_spi4_device,
#endif
#ifdef CONFIG_MUX_SDIO_OPT1_HAL
        &sprd_veth_sdio0_device,
        &sprd_veth_sdio1_device,
        &sprd_veth_sdio2_device,
        &sprd_veth_sdio3_device,
        &sprd_veth_sdio4_device,
#endif
#ifdef 	CONFIG_MODEM_INTF
&modem_interface_device,
#endif
#endif
#ifdef CONFIG_TS0710_MUX_ENABLE
	&sprd_mux_spi_device,
	&sprd_mux_sdio_device,
#endif
};

static struct platform_device *late_devices[] __initdata = {
	/* 1. CODECS */
	&sprd_audio_sprd_codec_v3_device,
	&sprd_audio_null_codec_device,

	/* 2. CPU DAIS */
	&sprd_audio_vbc_r2p0_device,
	&sprd_audio_vaudio_device,
	&sprd_audio_i2s0_device,
	&sprd_audio_i2s1_device,
	&sprd_audio_i2s2_device,
	&sprd_audio_i2s3_device,

	/* 3. PLATFORM */
	&sprd_audio_platform_pcm_device,

	/* 4. MACHINE */
	&sprd_audio_vbc_r2p0_sprd_codec_v3_device,
	&sprd_audio_i2s_null_codec_device,

};

/* BT suspend/resume */
static struct resource bluesleep_resources[] = {
	{
		.name	= "gpio_host_wake",
		.start	= GPIO_BT2AP_WAKE,
		.end	= GPIO_BT2AP_WAKE,
		.flags	= IORESOURCE_IO,
	},
	{
		.name	= "gpio_ext_wake",
		.start	= GPIO_AP2BT_WAKE,
		.end	= GPIO_AP2BT_WAKE,
		.flags	= IORESOURCE_IO,
	},
};

static struct platform_device brcm_bluesleep_device = {
	.name = "bluesleep",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(bluesleep_resources),
	.resource	= bluesleep_resources,
};

static struct resource rfkill_resources[] = {
	{
		.name   = "bt_power",
		.start  = GPIO_BT_POWER,
		.end    = GPIO_BT_POWER,
		.flags  = IORESOURCE_IO,
	},
	{
		.name   = "bt_reset",
		.start  = GPIO_BT_RESET,
		.end    = GPIO_BT_RESET,
		.flags  = IORESOURCE_IO,
	},
};
static struct platform_device rfkill_device = {
	.name = "rfkill",
	.id = -1,
	.num_resources	= ARRAY_SIZE(rfkill_resources),
	.resource	= rfkill_resources,
};

/* keypad backlight */
static struct platform_device kb_backlight_device = {
	.name           = "keyboard-backlight",
	.id             =  -1,
};

static int calibration_mode = false;
static int __init calibration_start(char *str)
{
	int calibration_device =0;
	int mode=0,freq=0,device=0;
	if(str){
		pr_info("modem calibartion:%s\n", str);
		sscanf(str, "%d,%d,%d", &mode,&freq,&device);
	}
	if(device & 0x80){
		calibration_device = device & 0xf0;
		calibration_mode = true;
		pr_info("calibration device = 0x%x\n",calibration_device);
	}
	return 1;
}
__setup("calibration=", calibration_start);

int in_calibration(void)
{
	return (int)(calibration_mode == true);
}

EXPORT_SYMBOL(in_calibration);

static int autotest_mode = false;
static int __init autotest_start(char *str)
{
	autotest_mode = true;
	return 1;
}
__setup("autotest=", autotest_start);

int in_autotest(void)
{
	return (int)(autotest_mode == true);
}

EXPORT_SYMBOL(in_autotest);

static void __init sprd_add_otg_device(void)
{
	/*
	 * if in calibrtaion mode, we do nothing, modem will handle everything
	 */
	platform_device_register(&sprd_otg_device);
}

static struct serial_data plat_data0 = {
	.wakeup_type = BT_RTS_HIGH_WHEN_SLEEP,
	.clk = 48000000,
};
static struct serial_data plat_data1 = {
	.wakeup_type = BT_RTS_HIGH_WHEN_SLEEP,
	.clk = 26000000,
};
static struct serial_data plat_data2 = {
	.wakeup_type = BT_RTS_HIGH_WHEN_SLEEP,
	.clk = 26000000,
};
#if(defined(CONFIG_TOUCHSCREEN_FOCALTECH)||defined(CONFIG_TOUCHSCREEN_FOCALTECH_MODULE))
static struct ft5x0x_ts_platform_data ft5x0x_ts_info = { 
	.irq_gpio_number    = GPIO_TOUCH_IRQ,
	.reset_gpio_number  = GPIO_TOUCH_RESET,
	.vdd_name           = "vdd28",
	.virtualkeys = {
	         130,1360,80,65,
	         280,1360,80,65,
	         630,1360,80,65
	         },
	 .TP_MAX_X = 720,
	 .TP_MAX_Y = 1280,
};
#endif

#if(defined(CONFIG_TOUCHSCREEN_MSG2138)||defined(CONFIG_TOUCHSCREEN_MSG2138_MODULE))
static struct msg2138_ts_platform_data msg2138_ts_info = {
	// // cg
	.irq_gpio_number	= GPIO_TOUCH_IRQ,
	.reset_gpio_number	= GPIO_TOUCH_RESET,
	.vdd_name 			= "vdd28",
	};
#endif


#if(defined(CONFIG_INPUT_LTR558_I2C)||defined(CONFIG_INPUT_LTR558_I2C_MODULE))
static struct ltr558_pls_platform_data ltr558_pls_info = {
	.irq_gpio_number	= GPIO_PROX_INT,
};
#endif

#if(defined(CONFIG_INPUT_LIS3DH_ZT_I2C)||defined(CONFIG_INPUT_LIS3DH_I2C_MODULE))
static struct lis3dh_acc_platform_data lis3dh_plat_data = {
	.poll_interval = 10,
	.min_interval = 10,
	.g_range = LIS3DH_ACC_G_2G,
	.axis_map_x = 1,
	.axis_map_y = 0,
	.axis_map_z = 2,
	.negate_x = 0,
	.negate_y = 1,
	.negate_z = 0
};
#endif

#if(defined(CONFIG_SENSORS_AK8975)||defined(CONFIG_SENSORS_AK8975_MODULE))
struct akm8975_platform_data akm8975_platform_d = {
	.mag_low_x = -20480,
	.mag_high_x = 20479,
	.mag_low_y = -20480,
	.mag_high_y = 20479,
	.mag_low_z = -20480,
	.mag_high_z = 20479,
};
#endif
#if(defined(CONFIG_SENSORS_AK8963)||defined(CONFIG_SENSORS_AK8963_MODULE))
struct akm8963_platform_data akm_platform_data_8963 = {
       .layout = 7,
       .outbit = 1,
       .gpio_DRDY = GPIO_M_DRDY,
       .gpio_RST = GPIO_M_RSTN,
};
#endif

#if(defined(CONFIG_INV_MPU_IIO)||defined(CONFIG_INV_MPU_IIO_MODULE))
static struct mpu_platform_data mpu9150_platform_data = {
	.int_config = 0x00,
	.level_shifter = 0,
	.vdd_name = "vddsim2",
	.orientation = { 1, 0, 0,
					  0, 1, 0,
					  0, 0, 1 },
	.sec_slave_type = SECONDARY_SLAVE_TYPE_NONE,
};
#endif

static struct i2c_board_info i2c2_boardinfo[] = {
#if(defined(CONFIG_SENSORS_AK8963)||defined(CONFIG_SENSORS_AK8963_MODULE))
       {  I2C_BOARD_INFO(AKM8963_I2C_NAME, AKM8963_I2C_ADDR),
          .platform_data = &akm_platform_data_8963,
       },
#endif
#if(defined(CONFIG_INPUT_LIS3DH_ZT_I2C)||defined(CONFIG_INPUT_LIS3DH_I2C_MODULE))
	{ I2C_BOARD_INFO(LIS3DH_ACC_I2C_NAME, LIS3DH_ACC_I2C_ADDR),
	  .platform_data = &lis3dh_plat_data,
	},
#endif
#if(defined(CONFIG_INV_MPU_IIO)||defined(CONFIG_INV_MPU_IIO_MODULE))
	{ I2C_BOARD_INFO("mpu9150", 0x68),
	  .irq = GPIO_GYRO_INT1,
	  .platform_data = &mpu9150_platform_data,
	},
#endif
#if(defined(CONFIG_INPUT_LTR558_I2C)||defined(CONFIG_INPUT_LTR558_I2C_MODULE))
	{ I2C_BOARD_INFO(LTR558_I2C_NAME,  LTR558_I2C_ADDR),
	  .platform_data = &ltr558_pls_info,
	},
#endif
#if(defined(CONFIG_SENSORS_AK8975)||defined(CONFIG_SENSORS_AK8975_MODULE))
	{ I2C_BOARD_INFO(AKM8975_I2C_NAME, AKM8975_I2C_ADDR),
	  .platform_data = &akm8975_platform_d,
	},
#endif
};

static struct i2c_board_info i2c0_boardinfo[] = {
	{I2C_BOARD_INFO("sensor_main",0x3C),},
	{I2C_BOARD_INFO("sensor_sub",0x21),},
};

static struct i2c_board_info i2c1_boardinfo[] = {
	{
		#if(defined(CONFIG_TOUCHSCREEN_MSG2138))
				I2C_BOARD_INFO("pixcir_ts", 0x26),
				.platform_data = &msg2138_ts_info,
		#endif		
	},
};

static int sc8810_add_i2c_devices(void)
{
	i2c_register_board_info(2, i2c2_boardinfo, ARRAY_SIZE(i2c2_boardinfo));
	i2c_register_board_info(1, i2c1_boardinfo, ARRAY_SIZE(i2c1_boardinfo));
	i2c_register_board_info(0, i2c0_boardinfo, ARRAY_SIZE(i2c0_boardinfo));
	return 0;
}

#if 0
/* Control ldo for maxscend cmmb chip according to HW design */
static struct regulator *cmmb_regulator_1v8 = NULL;

#define SPI_PIN_FUNC_MASK  (0x3<<4)
#define SPI_PIN_FUNC_DEF   (0x0<<4)
#define SPI_PIN_FUNC_GPIO  (0x3<<4)

struct spi_pin_desc {
	const char   *name;
	unsigned int pin_func;
	unsigned int reg;
	unsigned int gpio;
};

static struct spi_pin_desc spi_pin_group[] = {
	{"SPI_DI",  SPI_PIN_FUNC_DEF,  REG_PIN_SPI0_DI   + CTL_PIN_BASE,  158},
	{"SPI_CLK", SPI_PIN_FUNC_DEF,  REG_PIN_SPI0_CLK  + CTL_PIN_BASE,  159},
	{"SPI_DO",  SPI_PIN_FUNC_DEF,  REG_PIN_SPI0_DO   + CTL_PIN_BASE,  157},
	{"SPI_CS0", SPI_PIN_FUNC_GPIO, REG_PIN_SPI0_CSN  + CTL_PIN_BASE,  156}
};
static void sprd_restore_spi_pin_cfg(void)
{
	unsigned int reg;
	unsigned int  gpio;
	unsigned int  pin_func;
	unsigned int value;
	unsigned long flags;
	int i = 0;
	int regs_count = sizeof(spi_pin_group)/sizeof(struct spi_pin_desc);

	for (; i < regs_count; i++) {
	    pin_func = spi_pin_group[i].pin_func;
	    gpio = spi_pin_group[i].gpio;
	    if (pin_func == SPI_PIN_FUNC_DEF) {
		 reg = spi_pin_group[i].reg;
		 /* free the gpios that have request */
		 gpio_free(gpio);
		 local_irq_save(flags);
		 /* config pin default spi function */
		 value = ((__raw_readl(reg) & ~SPI_PIN_FUNC_MASK) | SPI_PIN_FUNC_DEF);
		 __raw_writel(value, reg);
		 local_irq_restore(flags);
	    }
	    else {
		 /* CS should config output */
		 gpio_direction_output(gpio, 1);
	    }
	}

}


static void sprd_set_spi_pin_input(void)
{
	unsigned int reg;
	unsigned int value;
	unsigned int  gpio;
	unsigned int  pin_func;
	const char    *name;
	unsigned long flags;
	int i = 0;

	int regs_count = sizeof(spi_pin_group)/sizeof(struct spi_pin_desc);

	for (; i < regs_count; i++) {
	    pin_func = spi_pin_group[i].pin_func;
	    gpio = spi_pin_group[i].gpio;
	    name = spi_pin_group[i].name;

	    /* config pin GPIO function */
	    if (pin_func == SPI_PIN_FUNC_DEF) {
		 reg = spi_pin_group[i].reg;

		 local_irq_save(flags);
		 value = ((__raw_readl(reg) & ~SPI_PIN_FUNC_MASK) | SPI_PIN_FUNC_GPIO);
		 __raw_writel(value, reg);
		 local_irq_restore(flags);
		 if (gpio_request(gpio, name)) {
		     printk("smsspi: request gpio %d failed, pin %s\n", gpio, name);
		 }

	    }

	    gpio_direction_input(gpio);
	}

}


static void mxd_cmmb_poweron(void)
{
        regulator_set_voltage(cmmb_regulator_1v8, 1700000, 1800000);
        regulator_disable(cmmb_regulator_1v8);
        msleep(3);
        regulator_enable(cmmb_regulator_1v8);
        msleep(5);

        /* enable 26M external clock */
        gpio_direction_output(GPIO_CMMB_26M_CLK_EN, 1);
}

static void mxd_cmmb_poweroff(void)
{
        regulator_disable(cmmb_regulator_1v8);
        gpio_direction_output(GPIO_CMMB_26M_CLK_EN, 0);
}

static int mxd_cmmb_init(void)
{
         int ret=0;
         ret = gpio_request(GPIO_CMMB_26M_CLK_EN,   "MXD_CMMB_CLKEN");
         if (ret)
         {
                   pr_debug("mxd spi req gpio clk en err!\n");
                   goto err_gpio_init;
         }
         gpio_direction_output(GPIO_CMMB_26M_CLK_EN, 0);
         cmmb_regulator_1v8 = regulator_get(NULL, "vddcmmb1p8");
         return 0;

err_gpio_init:
	 gpio_free(GPIO_CMMB_26M_CLK_EN);
         return ret;
}

static struct mxd_cmmb_026x_platform_data mxd_plat_data = {
	.poweron  = mxd_cmmb_poweron,
	.poweroff = mxd_cmmb_poweroff,
	.init     = mxd_cmmb_init,
	.set_spi_pin_input   = sprd_set_spi_pin_input,
	.restore_spi_pin_cfg = sprd_restore_spi_pin_cfg,
};

static int spi_cs_gpio_map[][2] = {
    {SPI0_CMMB_CS_GPIO,  0},
    {SPI0_CMMB_CS_GPIO,  0},
    {SPI0_CMMB_CS_GPIO,  0},
} ;

static struct spi_board_info spi_boardinfo[] = {
	{
	.modalias = "cmmb-dev",
	.bus_num = 0,
	.chip_select = 0,
	.max_speed_hz = 8 * 1000 * 1000,
	.mode = SPI_CPOL | SPI_CPHA,
        .platform_data = &mxd_plat_data,
	},
	{
	.modalias = "spidev",
	.bus_num = 1,
	.chip_select = 0,
	.max_speed_hz = 1000 * 1000,
	.mode = SPI_CPOL | SPI_CPHA,
	},
	{
	.modalias = "spidev",
	.bus_num = 2,
	.chip_select = 0,
	.max_speed_hz = 1000 * 1000,
	.mode = SPI_CPOL | SPI_CPHA,
	}
};
#endif

static struct spi_board_info spi_boardinfo[] = {
	{
	.modalias = "ipc-spi",
	.bus_num = 0,
	.chip_select = 0,
	.max_speed_hz = 1000 * 1000,
	.mode = SPI_CPOL | SPI_CPHA,
	}
};


static void sprd_spi_init(void)
{
	int busnum, cs, gpio;
	int i;

	struct spi_board_info *info = spi_boardinfo;

    spi_register_board_info(info, ARRAY_SIZE(spi_boardinfo));
}

static int sc8810_add_misc_devices(void)
{
	return 0;
}

int __init __clock_init_early(void)
{
	pr_info("ahb ctl0 %08x, ctl2 %08x glb aon apb0 %08x aon apb1 %08x clk_en %08x\n",
		sci_glb_raw_read(REG_AP_AHB_AHB_EB),
		sci_glb_raw_read(REG_AP_AHB_AHB_RST),
		sci_glb_raw_read(REG_AON_APB_APB_EB0),
		sci_glb_raw_read(REG_AON_APB_APB_EB1),
		sci_glb_raw_read(REG_AON_CLK_PUB_AHB_CFG));

	sci_glb_clr(REG_AP_AHB_AHB_EB,
		BIT_BUSMON2_EB		|
		BIT_BUSMON1_EB		|
		BIT_BUSMON0_EB		|
		//BIT_SPINLOCK_EB		|
		BIT_GPS_EB		|
		//BIT_EMMC_EB		|
		//BIT_SDIO2_EB		|
		//BIT_SDIO1_EB		|
		//BIT_SDIO0_EB		|
		BIT_DRM_EB		|
		BIT_NFC_EB		|
		//BIT_DMA_EB		|
		//BIT_USB_EB		|
		//BIT_GSP_EB		|
		//BIT_DISPC1_EB		|
		//BIT_DISPC0_EB		|
		//BIT_DSI_EB		|
		0);
	sci_glb_clr(REG_AP_APB_APB_EB,
		BIT_INTC3_EB		|
		BIT_INTC2_EB		|
		BIT_INTC1_EB		|
		BIT_IIS1_EB		|
		BIT_UART2_EB		|
		BIT_UART0_EB		|
		BIT_SPI1_EB		|
		BIT_SPI0_EB		|
		BIT_IIS0_EB		|
		BIT_I2C0_EB		|
		BIT_SPI2_EB		|
		BIT_UART3_EB		|
		0);
	sci_glb_clr(REG_AON_APB_APB_RTC_EB,
		BIT_KPD_RTC_EB		|
		BIT_KPD_EB		|
		BIT_EFUSE_EB		|
		0);

	sci_glb_clr(REG_AON_APB_APB_EB0,
		BIT_AUDIF_EB			|
		BIT_VBC_EB			|
//		BIT_PWM3_EB			|
		BIT_PWM1_EB			|
		0);
	sci_glb_clr(REG_AON_APB_APB_EB1,
		BIT_AUX1_EB			|
		BIT_AUX0_EB			|
		0);

	printk("sc clock module early init ok\n");
	return 0;
}

static inline int	__sci_get_chip_id(void)
{
	return __raw_readl(CHIP_ID_LOW_REG);
}

#define REG_ANA_PIN_EXL_EN0	SCI_ADDR(SPRD_ANA_PIN_BASE, 0x50)

static u32 cp_avdd18_en = 0;
static u32 ana_pin_exl_en0_str = 0;
static int cp_avdd18_en_set(u32 val)
{
	cp_avdd18_en = val;
	printk("cp_avdd18_en_set %d\n", cp_avdd18_en);

	ana_pin_exl_en0_str = sci_adi_read(REG_ANA_PIN_EXL_EN0);
	if(cp_avdd18_en) {
		sci_adi_set(ANA_REG_GLB_LDO_DCDC_PD_RTCSET, BIT_15);
		sci_adi_clr(ANA_REG_GLB_LDO_DCDC_PD_RTCCLR, BIT_15);

		/* pinmap cfg */
		sci_adi_clr(REG_ANA_PIN_EXL_EN0, 0x33); /* clear BIT_5 | BIT_4 | BIT_1 | BIT_0 */
		sci_adi_set(REG_ANA_PIN_EXL_EN0, 0x2);   /* set bit_2 1 */

		sci_adi_clr(ANA_REG_GLB_LDO_SLP_CTRL1, BIT_SLP_LDOAVDD18_PD_EN);/* the sleep of AVDD1V8 cannot be controled by shark */
		sci_adi_set(ANA_REG_GLB_PWR_XTL_EN2, BIT_LDO_AVDD18_EXT_XTL0_EN);/* the sleep of AVDD1V8 can be controled by 9620 */
	}
	else {
		sci_adi_clr(ANA_REG_GLB_PWR_XTL_EN2, BIT_LDO_AVDD18_EXT_XTL0_EN);

		sci_adi_set(ANA_REG_GLB_LDO_SLP_CTRL1, BIT_SLP_LDOAVDD18_PD_EN);

		sci_adi_set(REG_ANA_PIN_EXL_EN0, ana_pin_exl_en0_str);

		sci_adi_clr(ANA_REG_GLB_LDO_DCDC_PD_RTCSET, BIT_15);
		sci_adi_set(ANA_REG_GLB_LDO_DCDC_PD_RTCCLR, BIT_15);

	}
	return 0;
}

#define cp_attr(_name) \
static struct kobj_attribute _name##_attr = {	\
	.attr	= {				\
		.name = __stringify(_name),	\
		.mode = 0644,			\
	},					\
	.show	= _name##_show,			\
	.store	= _name##_store,		\
}

static ssize_t cp_ldo_show(struct kobject *kobj, struct kobj_attribute *attr,
			  char *buf)
{
	int err;
	err = sprintf(buf, "%d \n", cp_avdd18_en);
	return err;
}
static ssize_t cp_ldo_store(struct kobject *kobj, struct kobj_attribute *attr,
			   const char *buf, size_t n)
{
	printk("cp_ldo_store %c\n", buf[0]);
	switch(buf[0]) {
	case '0':
		cp_avdd18_en_set(0);
		break;
	case '1' :
		cp_avdd18_en_set(1);
		break;
	default:
		break;
	}
	return 1;
}

cp_attr(cp_ldo);
static struct attribute * g[] = {
	&cp_ldo_attr.attr,
	NULL,
};
static struct attribute_group attr_group = {
	.attrs = g,
};

struct kobject *cp_ldo_kobj;
static int cp_ldo_ctrl_fs_creat(void)
{
	cp_ldo_kobj = kobject_create_and_add("cp_ldo", NULL);
	if (!cp_ldo_kobj)
		return -ENOMEM;
	return sysfs_create_group(cp_ldo_kobj, &attr_group);
}
/*i2s0 config for BT, use pcm mode*/
static struct i2s_config i2s0_config = {
	.fs = 8000,
	.slave_timeout = 0xF11,
	.bus_type = PCM_BUS,
	.byte_per_chan = I2S_BPCH_16,
	.mode = I2S_MASTER,
	.lsb = I2S_LSB,
	.rtx_mode = I2S_RTX_MODE,
	.sync_mode = I2S_LRCK,
	.lrck_inv = I2S_L_LEFT,	/*NOTE: MUST I2S_L_LEFT in pcm mode*/
	.clk_inv = I2S_CLK_N,
	.pcm_bus_mode = I2S_SHORT_FRAME,
	.pcm_slot = 0x1,
	.pcm_cycle = 1,
	.tx_watermark = 8,
	.rx_watermark = 24,
};
/*you can change param here to config the i2s modules*/
static struct i2s_config i2s1_config = {0};
static struct i2s_config i2s2_config = {0};
static struct i2s_config i2s3_config = {0};

static void __init sc8830_init_machine(void)
{
	printk("sci get chip id = 0x%x\n",__sci_get_chip_id());

	sci_adc_init((void __iomem *)ADC_BASE);
	sci_regulator_init();
    #if defined(CONFIG_ION) && defined(SPRD_ION_BASE_USE_VARIABLE)
	{
	    extern void init_ion_addr_param(void);
	    init_ion_addr_param();
	}
    #endif
    #if defined(CONFIG_PSTORE_RAM) && defined(SPRD_ION_BASE_USE_VARIABLE)
	{
	    extern void init_pstore_addr_param(void);
	    init_pstore_addr_param();
	}
    #endif
	sprd_add_otg_device();
	platform_device_add_data(&sprd_serial_device0,(const void*)&plat_data0,sizeof(plat_data0));
	platform_device_add_data(&sprd_serial_device1,(const void*)&plat_data1,sizeof(plat_data1));
	platform_device_add_data(&sprd_serial_device2,(const void*)&plat_data2,sizeof(plat_data2));
	platform_device_add_data(&sprd_keypad_device,(const void*)&sci_keypad_data,sizeof(sci_keypad_data));
	platform_device_add_data(&sprd_audio_i2s0_device,(const void*)&i2s0_config,sizeof(i2s0_config));
	platform_device_add_data(&sprd_audio_i2s1_device,(const void*)&i2s1_config,sizeof(i2s1_config));
	platform_device_add_data(&sprd_audio_i2s2_device,(const void*)&i2s2_config,sizeof(i2s2_config));
	platform_device_add_data(&sprd_audio_i2s3_device,(const void*)&i2s3_config,sizeof(i2s3_config));
	platform_add_devices(devices, ARRAY_SIZE(devices));
	sc8810_add_i2c_devices();
	sc8810_add_misc_devices();
	sprd_spi_init();
	sprd_sr2351_gpio_ctrl_power_register(GPIO_RF2351_POWER_CTRL_NUM);
}

static void __init sc8830_init_late(void)
{
	platform_add_devices(late_devices, ARRAY_SIZE(late_devices));
}

extern void __init  sci_enable_timer_early(void);
static void __init sc8830_init_early(void)
{
	/* earlier init request than irq and timer */
	__clock_init_early();
	sci_enable_timer_early();
	sci_adi_init();
	/*ipi reg init for sipc*/
	sci_glb_set(REG_AON_APB_APB_EB0, BIT_IPI_EB);
}
extern struct smp_operations sprd_smp_ops;
int __init cp_ldo_init(void)
{
	printk("cp_ldo_init\n");
	cp_ldo_ctrl_fs_creat();
}
arch_initcall(cp_ldo_init);
MACHINE_START(SCPHONE, "sc8830")
	.smp		= smp_ops(sprd_smp_ops),
	.reserve	= sci_reserve,
	.map_io		= sci_map_io,
	.init_early	= sc8830_init_early,
	.init_irq	= sci_init_irq,
	.init_time		= sci_timer_init,
	.init_machine	= sc8830_init_machine,
	.init_late	= sc8830_init_late,
MACHINE_END

