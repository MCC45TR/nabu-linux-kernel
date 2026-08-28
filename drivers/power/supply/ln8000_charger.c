// SPDX-License-Identifier: GPL-2.0-only
/*
 * ln8000-charger.c - Charger driver for LIONSEMI LN8000
 *
 * Copyright (C) 2021 Lion Semiconductor Inc.
 * Copyright (C) 2021 XiaoMi, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/property.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeup.h>
#include <linux/power_supply.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/sysfs.h>
#include <linux/types.h>

/**
 * ln8000 device descripion definition
 */
#define ASSIGNED_BITS(_end, _start) ((BIT(_end) - BIT(_start)) + BIT(_end))

/* register map description */
enum ln8000_int1_desc {
	LN8000_MASK_FAULT_INT = BIT(7),
	LN8000_MASK_NTC_PROT_INT = BIT(6),
	LN8000_MASK_CHARGE_PHASE_INT = BIT(5),
	LN8000_MASK_MODE_INT = BIT(4),
	LN8000_MASK_REV_CURR_INT = BIT(3),
	LN8000_MASK_TEMP_INT = BIT(2),
	LN8000_MASK_ADC_DONE_INT = BIT(1),
	LN8000_MASK_TIMER_INT = BIT(0),
};

enum ln8000_sys_sts_desc {
	LN8000_MASK_IIN_LOOP_STS = BIT(7),
	LN8000_MASK_VFLOAT_LOOP_STS = BIT(6),
	LN8000_MASK_BYPASS_ENABLED = BIT(3),
	LN8000_MASK_SWITCHING_ENABLED = BIT(2),
	LN8000_MASK_STANDBY_STS = BIT(1),
	LN8000_MASK_SHUTDOWN_STS = BIT(0),
};

enum ln8000_safety_sts_desc {
	LN8000_MASK_TEMP_MAX_STS = BIT(6),
	LN8000_MASK_TEMP_REGULATION_STS = BIT(5),
	LN8000_MASK_NTC_ALARM_STS = BIT(4),
	LN8000_MASK_NTC_SHUTDOWN_STS = BIT(3),
	LN8000_MASK_REV_IIN_STS = BIT(2),
};

enum ln8000_fault1_sts_desc {
	LN8000_MASK_WATCHDOG_TIMER_STS = BIT(7),
	LN8000_MASK_VBAT_OV_STS = BIT(6),
	LN8000_MASK_VAC_UNPLUG_STS = BIT(4),
	LN8000_MASK_VAC_OV_STS = BIT(3),
	LN8000_MASK_VIN_OV_STS = BIT(1),
	LN8000_MASK_VFAULTS = ASSIGNED_BITS(6, 0),
};

enum ln8000_fault2_sts_desc {
	LN8000_MASK_IIN_OC_DETECTED = BIT(7),
};

enum ln8000_ldo_sts_desc {
	LN8000_MASK_VBAT_MIN_OK_STS = BIT(7),
	LN8000_MASK_CHARGE_TERM_STS = BIT(5),
	LN8000_MASK_RECHARGE_STS = BIT(4),
};

enum ln8000_regulation_ctrl_desc {
	LN8000_BIT_ENABLE_VFLOAT_LOOP_INT = 7,
	LN8000_BIT_ENABLE_IIN_LOOP_INT = 6,
	LN8000_BIT_DISABLE_VFLOAT_LOOP = 5,
	LN8000_BIT_DISABLE_IIN_LOOP = 4,
	LN8000_BIT_TEMP_MAX_EN = 2,
	LN8000_BIT_TEMP_REG_EN = 1,
};

enum ln8000_sys_ctrl_desc {
	LN8000_BIT_STANDBY_EN = 3,
	LN8000_BIT_REV_IIN_DET = 2,
	LN8000_BIT_EN_1TO1 = 0,
};

enum ln8000_fault_ctrl_desc {
	LN8000_BIT_DISABLE_IIN_OCP = 6,
	LN8000_BIT_DISABLE_VBAT_OV = 5,
	LN8000_BIT_DISABLE_VAC_OV = 4,
	LN8000_BIT_DISABLE_VAC_UV = 3,
	LN8000_BIT_DISABLE_VIN_OV = 2,
};

enum ln8000_bc_op1_desc {
	LN8000_BIT_DUAL_FUNCTION_EN = 2,
	LN8000_BIT_DUAL_CFG = 1,
	LN8000_BIT_DUAL_LOCKOUT_EN = 0,
};

enum ln8000_reg_addr {
	LN8000_REG_DEVICE_ID = 0x00,
	LN8000_REG_INT1 = 0x01,
	LN8000_REG_INT1_MSK = 0x02,
	LN8000_REG_SYS_STS = 0x03,
	LN8000_REG_SAFETY_STS = 0x04,
	LN8000_REG_FAULT1_STS = 0x05,
	LN8000_REG_FAULT2_STS = 0x06,
	LN8000_REG_CURR1_STS = 0x07,
	LN8000_REG_LDO_STS = 0x08,
	LN8000_REG_ADC01_STS = 0x09,
	LN8000_REG_ADC02_STS = 0x0A,
	LN8000_REG_ADC03_STS = 0x0B,
	LN8000_REG_ADC04_STS = 0x0C,
	LN8000_REG_ADC05_STS = 0x0D,
	LN8000_REG_ADC06_STS = 0x0E,
	LN8000_REG_ADC07_STS = 0x0F,
	LN8000_REG_ADC08_STS = 0x10,
	LN8000_REG_ADC09_STS = 0x11,
	LN8000_REG_ADC10_STS = 0x12,
	LN8000_REG_IIN_CTRL = 0x1B,
	LN8000_REG_REGULATION_CTRL = 0x1C,
	LN8000_REG_PWR_CTRL = 0x1D,
	LN8000_REG_SYS_CTRL = 0x1E,
	LN8000_REG_LDO_CTRL = 0x1F,
	LN8000_REG_GLITCH_CTRL = 0x20,
	LN8000_REG_FAULT_CTRL = 0x21,
	LN8000_REG_NTC_CTRL = 0x22,
	LN8000_REG_ADC_CTRL = 0x23,
	LN8000_REG_ADC_CFG = 0x24,
	LN8000_REG_RECOVERY_CTRL = 0x25,
	LN8000_REG_TIMER_CTRL = 0x26,
	LN8000_REG_THRESHOLD_CTRL = 0x27,
	LN8000_REG_V_FLOAT_CTRL = 0x28,
	LN8000_REG_CHARGE_CTRL = 0x29,
	LN8000_REG_LION_CTRL = 0x30,
	LN8000_REG_BC_OP_1 = 0x41,
	LN8000_REG_BC_OP_2 = 0x42,
	LN8000_REG_BC_STS_A = 0x49,
	LN8000_REG_BC_STS_B = 0x4A,
	LN8000_REG_BC_STS_C = 0x4B,
	LN8000_REG_BC_STS_D = 0x4C,
	LN8000_REG_BC_STS_E = 0x4D,
	LN8000_REG_MAX,
};

/* device feature configuration desc */
#define LN8000_DEVICE_ID 0x42

enum ln8000_opmode_ {
	LN8000_OPMODE_UNKNOWN = 0x0,
	LN8000_OPMODE_STANDBY = 0x1,
	LN8000_OPMODE_BYPASS = 0x2,
	LN8000_OPMODE_SWITCHING = 0x3,
};

enum ln8000_vac_ov_cfg_desc {
	LN8000_VAC_OVP_6P5V = 0x0,
	LN8000_VAC_OVP_11V = 0x1,
	LN8000_VAC_OVP_12V = 0x2,
	LN8000_VAC_OVP_13V = 0x3,
};

enum ln8000_watchdpg_cfg_desc {
	LN8000_WATCHDOG_5SEC = 0x0,
	LN8000_WATCHDOG_10SEC = 0x1,
	LN8000_WATCHDOG_20SEC = 0x2,
	LN8000_WATCHDOG_40SEC = 0x3,
	LN8000_WATCHDOG_MAX
};

enum ln8000_adc_channel_index {
	LN8000_ADC_CH_VOUT = 1,
	LN8000_ADC_CH_VIN,
	LN8000_ADC_CH_VBAT,
	LN8000_ADC_CH_VAC,
	LN8000_ADC_CH_IIN,
	LN8000_ADC_CH_DIETEMP,
	LN8000_ADC_CH_TSBAT,
	LN8000_ADC_CH_TSBUS,
	LN8000_ADC_CH_ALL
};

enum ln8000_adc_mode_desc { /* used FORCE_ADC_MODE + ADC_SHUTDOWN_CFG */
			    ADC_AUTO_HIB_MODE = 0x0,
			    ADC_AUTO_SHD_MODE = 0x1,
			    ADC_SHUTDOWN_MODE = 0x2,
			    ADC_HIBERNATE_MODE = 0x4,
			    ADC_NORMAL_MODE = 0x6,
};

enum ln8000_adc_hibernate_delay_desc {
	ADC_HIBERNATE_500MS = 0x0,
	ADC_HIBERNATE_1S = 0x1,
	ADC_HIBERNATE_2S = 0x2,
	ADC_HIBERNATE_4S = 0x3,
};

/* electrical numeric calculation unit description */
#define LN8000_VBAT_FLOAT_MIN 3725000 /* unit = uV */
#define LN8000_VBAT_FLOAT_MAX 5000000
#define LN8000_VBAT_FLOAT_LSB 5000
#define LN8000_ADC_VOUT_STEP 5000 /* 5mV= 5000uV LSB	(0V ~ 5.115V) */
#define LN8000_ADC_VIN_STEP 16000 /* 16mV=16000uV LSB	(0V ~ 16.386V) */
#define LN8000_ADC_VBAT_STEP 5000 /* 5mV= 5000uV LSB	(0V ~ 5.115V) */
#define LN8000_ADC_VBAT_MIN 1000000 /* 1V */
#define LN8000_ADC_VAC_STEP 16000 /* 16mV=16000uV LSB	(0V ~ 16.386V) */
#define LN8000_ADC_VAC_OS 5
#define LN8000_ADC_IIN_STEP 4890 /* 4.89mA=4890uA LSB	(0A ~ 5A) */
#define LN8000_ADC_DIETEMP_STEP \
	4350 /* 0.435C LSB = 4350dC/1000 (-25C ~ 160C) */
#define LN8000_ADC_DIETEMP_DENOM 1000 /* 1000 */
#define LN8000_ADC_DIETEMP_MIN (-250) /* -25C = -250dC */
#define LN8000_ADC_DIETEMP_MAX 1600 /* 160C = 1600dC */
#define LN8000_ADC_NTCV_STEP 2933 /* 2.933mV=2933uV LSB	(0V ~ 3V) */
#define LN8000_IIN_CFG_MIN 500000 /* 500mA=500,000uA */
#define LN8000_IIN_CFG_LSB 50000 /* 50mA=50,000uA */

/* device default values */
#define LN8000_BAT_OVP_DEFAULT 4440000
#define LN8000_BUS_OVP_DEFAULT 9500000
#define LN8000_BUS_OCP_DEFAULT 2000000

#define LN8000_NTC_ALARM_CFG_DEFAULT 226 /* NTC alarm threshold (~40C) */
#define LN8000_NTC_SHUTDOWN_CFG 2 /* NTC shutdown config (-16LSB ~ 4.3C) */
#define LN8000_DEFAULT_FSW_CFG 8 /* 8=440kHz, switching freq */
#define LN8000_IIN_CFG_DEFAULT 2000000 /* 2A=2,000,000uA, input current limit */

/**
 * driver instance structure definition
 */
struct ln8000_platform_data {
	struct gpio_desc *irq_gpio;

	/* feature configuration */
	unsigned int bat_ovp_th; /* battery OVP threshold (uV) */
	unsigned int bat_ovp_alarm_th; /* battery OVP alarm threshold (uV) */
	unsigned int bus_ovp_th; /* input OVP threshold (uV) */
	unsigned int bus_ovp_alarm_th; /* input OVP alarm threshold (uV) */
	unsigned int bus_ocp_th; /* input OCP threshold (uA) */
	unsigned int bus_ocp_alarm_th; /* IIN ocp alarm threshold */
	unsigned int
		ntc_alarm_cfg; /* input/battery NTC voltage threshold code: 0~1023 */
	unsigned int min_input_uv; /* minimum safe voltage for 2:1 switching */
	bool input_ntc_unwired; /* board has no thermistor on the TSBUS input */
};

struct ln8000_info {
	struct device *dev;
	struct i2c_client *client;
	struct ln8000_platform_data *pdata;
	struct power_supply *psy_chg;
	struct power_supply *typec_psy;
	struct power_supply *switching_psy;

	struct notifier_block nb;
	struct delayed_work status_changed_work;
	struct delayed_work charge_work;
	int status;
	bool notifier_registered;
	bool irq_wake_enabled;
	bool switching_charger_suspended;
	bool suspended;

	struct mutex data_lock;
	struct mutex i2c_lock;
	struct mutex irq_lock;
	struct regmap *regmap;
	struct power_supply_config psy_cfg;
	struct power_supply_desc psy_desc;

	unsigned int op_mode; /* target operation mode */
	unsigned int pwr_status; /* current device status */

	/* system/device status */
	bool vbat_regulated; /* vbat loop is active (+ OV alarm) */
	bool iin_regulated; /* iin loop is active (+ OC alarm) */
	bool tdie_fault; /* die temperature fault */
	bool tbus_tbat_fault; /* BUS/BAT temperature fault */
	bool tdie_alarm; /* die temperature alarm (regulated) */
	bool tbus_tbat_alarm; /* BUS/BAT temperature alarm */
	bool wdt_fault; /* watchdog timer expiration */
	bool vbat_ov; /* vbat OV fault */
	bool vac_ov; /* vac OV fault */
	bool vbus_ov; /* vbus OV fault */
	bool iin_oc; /* iin OC fault */
	bool vac_unplug; /* vac unplugged */
	bool iin_rc; /* iin reverse current detected */
	bool volt_qual; /* all voltages are qualified */
	bool usb_present; /* usb plugged (present) */
	bool chg_en; /* charging enavbled */
	bool rcp_en; /* reverse current protection enabled */
	int vbat_ovp_alarm_th; /* vbat ovp alarm threshold */
	int vin_ovp_alarm_th; /* vin ovp alarm threshold */
	int iin_ocp_alarm_th; /* iin ocp alarm threshold */

	/* ADC readings */
	int tbat_uV; /* BAT temperature (NTC, uV) */
	int tbus_uV; /* BUS temperature (NTC, uV) */
	int tdie_dC; /* die temperature (deci-Celsius) */
	int vbat_uV; /* battery voltage (uV) */
	int vbus_uV; /* input voltage (uV) */
	int iin_uA; /* input current (uV) */
};

static int psy_chg_set_charging_enable(struct ln8000_info *info, int val);

#define ln_err(fmt, ...)                                          \
	do {                                                      \
		printk(KERN_ERR "ln8000@pri: %s: " fmt, __func__, \
		       ##__VA_ARGS__);                            \
	} while (0);

#define ln_info(fmt, ...)                                          \
	do {                                                       \
		printk(KERN_INFO "ln8000@pri: %s: " fmt, __func__, \
		       ##__VA_ARGS__);                             \
	} while (0);

#define ln_dbg(fmt, ...)                                            \
	do {                                                        \
		printk(KERN_DEBUG "ln8000@pri: %s: " fmt, __func__, \
		       ##__VA_ARGS__);                              \
	} while (0);

#define LN8000_REG_PRINT(reg_addr, val)                                     \
	do {                                                                \
		ln_info("  --> [%-20s]   0x%02X   :   0x%02X\n", #reg_addr, \
			LN8000_REG_##reg_addr, (val) & 0xFF);               \
	} while (0);

#define LN8000_BIT_CHECK(val, idx, desc)          \
	do {                                      \
		if ((val) & BIT(idx))             \
			ln_info("-> %s\n", desc); \
	} while (0)
#define LN8000_USE_GPIO(pdata) \
	((pdata != NULL) && (!IS_ERR_OR_NULL(pdata->irq_gpio)))
#define LN8000_STATUS(val, mask) ((val & mask) ? true : false)

/**
 * I2C control functions : when occurred I2C tranfer fault, we
 * will retry to it. (default count:3)
 */
#define I2C_RETRY_CNT 3
static int ln8000_read_reg(struct ln8000_info *info, u8 addr,
			   unsigned int *data)
{
	int i, ret = 0;

	mutex_lock(&info->i2c_lock);
	for (i = 0; i < I2C_RETRY_CNT; ++i) {
		ret = regmap_read(info->regmap, addr, data);
		if (IS_ERR_VALUE((unsigned long)ret)) {
			ln_info("failed-read, reg(0x%02X), ret(%d)\n", addr,
				ret);
		} else {
			break;
		}
	}
	mutex_unlock(&info->i2c_lock);
	return ret;
}

static int ln8000_bulk_read_reg(struct ln8000_info *info, u8 addr, void *data,
				int count)
{
	int i, ret = 0;

	mutex_lock(&info->i2c_lock);
	for (i = 0; i < I2C_RETRY_CNT; ++i) {
		ret = regmap_bulk_read(info->regmap, addr, data, count);
		if (IS_ERR_VALUE((unsigned long)ret)) {
			ln_info("failed-bulk-read, reg(0x%02X, %d bytes), ret(%d)\n",
				addr, count, ret);
		} else {
			break;
		}
	}
	mutex_unlock(&info->i2c_lock);
	return ret;
}

static int ln8000_write_reg(struct ln8000_info *info, u8 addr, u8 data)
{
	int i, ret = 0;

	mutex_lock(&info->i2c_lock);
	for (i = 0; i < I2C_RETRY_CNT; ++i) {
		ret = regmap_write(info->regmap, addr, data);
		if (IS_ERR_VALUE((unsigned long)ret)) {
			ln_info("failed-write, reg(0x%02X), ret(%d)\n", addr,
				ret);
		} else {
			break;
		}
	}
	mutex_unlock(&info->i2c_lock);
	return ret;
}

static int ln8000_update_reg(struct ln8000_info *info, u8 addr, u8 mask,
			     u8 data)
{
	int i, ret = 0;

	mutex_lock(&info->i2c_lock);
	for (i = 0; i < I2C_RETRY_CNT; ++i) {
		ret = regmap_update_bits(info->regmap, addr, mask, data);
		if (IS_ERR_VALUE((unsigned long)ret)) {
			ln_info("failed-update, reg(0x%02X), ret(%d)\n", addr,
				ret);
		} else {
			break;
		}
	}
	mutex_unlock(&info->i2c_lock);
	return ret;
}

static int ln8000_set_vac_ovp(struct ln8000_info *info, unsigned int ovp_th)
{
	u8 cfg;

	if (ovp_th <= 6500000) {
		cfg = LN8000_VAC_OVP_6P5V;
	} else if (ovp_th <= 11000000) {
		cfg = LN8000_VAC_OVP_11V;
	} else if (ovp_th <= 12000000) {
		cfg = LN8000_VAC_OVP_12V;
	} else {
		cfg = LN8000_VAC_OVP_13V;
	}

	return ln8000_update_reg(info, LN8000_REG_GLITCH_CTRL, 0x3 << 2,
				 cfg << 2);
}

/* battery float voltage */
static int ln8000_set_vbat_float(struct ln8000_info *info, unsigned int cfg)
{
	u8 val;

	if (cfg < LN8000_VBAT_FLOAT_MIN)
		val = 0x00;
	else if (cfg > LN8000_VBAT_FLOAT_MAX)
		val = 0xFF;
	else
		val = (cfg - LN8000_VBAT_FLOAT_MIN) / LN8000_VBAT_FLOAT_LSB;

	return ln8000_write_reg(info, LN8000_REG_V_FLOAT_CTRL, val);
}

static int ln8000_set_iin_limit(struct ln8000_info *info, unsigned int cfg)
{
	u8 val = cfg / LN8000_IIN_CFG_LSB;

	ln_info("iin_limit=%dmV(iin_ctrl=0x%x)\n", cfg / 1000, val);

	return ln8000_update_reg(info, LN8000_REG_IIN_CTRL, 0x7F, val);
}

static int ln8000_set_ntc_alarm(struct ln8000_info *info, unsigned int cfg)
{
	int ret;

	/* update lower bits */
	ret = ln8000_write_reg(info, LN8000_REG_NTC_CTRL, (cfg & 0xFF));
	if (ret < 0)
		return ret;

	/* update upper bits */
	ret = ln8000_update_reg(info, LN8000_REG_ADC_CTRL, 0x3, (cfg >> 8));
	return ret;
}

/* battery voltage OV protection */
static int ln8000_enable_vbat_ovp(struct ln8000_info *info, bool enable)
{
	u8 val;

	val = (enable) ? 0 : 1; //disable
	val <<= LN8000_BIT_DISABLE_VBAT_OV;

	return ln8000_update_reg(info, LN8000_REG_FAULT_CTRL,
				 BIT(LN8000_BIT_DISABLE_VBAT_OV), val);
}

static int ln8000_enable_vbat_regulation(struct ln8000_info *info, bool enable)
{
	return ln8000_update_reg(info, LN8000_REG_REGULATION_CTRL,
				 0x1 << LN8000_BIT_DISABLE_VFLOAT_LOOP,
				 !(enable) << LN8000_BIT_DISABLE_VFLOAT_LOOP);
}

static int ln8000_enable_vbat_loop_int(struct ln8000_info *info, bool enable)
{
	return ln8000_update_reg(info, LN8000_REG_REGULATION_CTRL,
				 0x1 << LN8000_BIT_ENABLE_VFLOAT_LOOP_INT,
				 enable << LN8000_BIT_ENABLE_VFLOAT_LOOP_INT);
}

/* input current OC protection */
static int ln8000_enable_iin_ocp(struct ln8000_info *info, bool enable)
{
	return ln8000_update_reg(info, LN8000_REG_FAULT_CTRL,
				 0x1 << LN8000_BIT_DISABLE_IIN_OCP,
				 !(enable) << LN8000_BIT_DISABLE_IIN_OCP);
}

static int ln8000_enable_iin_regulation(struct ln8000_info *info, bool enable)
{
	return ln8000_update_reg(info, LN8000_REG_REGULATION_CTRL,
				 0x1 << LN8000_BIT_DISABLE_IIN_LOOP,
				 !(enable) << LN8000_BIT_DISABLE_IIN_LOOP);
}

static int ln8000_enable_iin_loop_int(struct ln8000_info *info, bool enable)
{
	return ln8000_update_reg(info, LN8000_REG_REGULATION_CTRL,
				 0x1 << LN8000_BIT_ENABLE_IIN_LOOP_INT,
				 enable << LN8000_BIT_ENABLE_IIN_LOOP_INT);
}

static int ln8000_enable_vac_ov(struct ln8000_info *info, bool enable)
{
	return ln8000_update_reg(info, LN8000_REG_FAULT_CTRL,
				 0x1 << LN8000_BIT_DISABLE_VAC_OV,
				 !(enable) << LN8000_BIT_DISABLE_VAC_OV);
}

static int ln8000_enable_tdie_prot(struct ln8000_info *info, bool enable)
{
	return ln8000_update_reg(info, LN8000_REG_REGULATION_CTRL,
				 0x1 << LN8000_BIT_TEMP_MAX_EN,
				 enable << LN8000_BIT_TEMP_MAX_EN);
}

static int ln8000_enable_tdie_regulation(struct ln8000_info *info, bool enable)
{
	return ln8000_update_reg(info, LN8000_REG_REGULATION_CTRL,
				 0x1 << LN8000_BIT_TEMP_REG_EN,
				 enable << LN8000_BIT_TEMP_REG_EN);
}

/* ADC channel enable */
static int ln8000_set_adc_ch(struct ln8000_info *info, unsigned int ch,
			     bool enable)
{
	u8 mask;
	u8 val;
	int ret;

	if ((ch > LN8000_ADC_CH_ALL) || (ch < 1))
		return -EINVAL;

	if (ch == LN8000_ADC_CH_ALL) {
		// update all channels
		val = (enable) ? 0x3E : 0x00;
		ret = ln8000_write_reg(info, LN8000_REG_ADC_CFG, val);
	} else {
		// update selected channel
		mask = 1 << (ch - 1);
		val = (enable) ? 1 : 0;
		val <<= (ch - 1);
		ret = ln8000_update_reg(info, LN8000_REG_ADC_CFG, mask, val);
	}

	return ret;
}

/* BUS temperature monitoring (protection+alarm) */
static int ln8000_enable_tbus_monitor(struct ln8000_info *info, bool enable)
{
	int ret = 0;

	/* enable BUS monitoring */
	ret = ln8000_update_reg(info, LN8000_REG_RECOVERY_CTRL, 0x1 << 1,
				enable << 1);
	if (ret < 0)
		return ret;

	/* enable BUS ADC channel */
	if (enable) {
		ret = ln8000_set_adc_ch(info, LN8000_ADC_CH_TSBUS, true);
	}
	return ret;
}

/* BAT temperature monitoring (protection+alarm) */
static int ln8000_enable_tbat_monitor(struct ln8000_info *info, bool enable)
{
	int ret = 0;

	/* enable BAT monitoring */
	ret = ln8000_update_reg(info, LN8000_REG_RECOVERY_CTRL, 0x1 << 0,
				enable << 0);
	if (ret < 0)
		return ret;

	/* enable BAT ADC channel */
	if (enable) {
		ret = ln8000_set_adc_ch(info, LN8000_ADC_CH_TSBAT, true);
	}
	return ret;
}

/* watchdog timer */
static int ln8000_enable_wdt(struct ln8000_info *info, bool enable)
{
	return ln8000_update_reg(info, LN8000_REG_TIMER_CTRL, 0x1 << 7,
				 enable << 7);
}

/* unplug / reverse-current detection */
static int ln8000_enable_rcp(struct ln8000_info *info, bool enable)
{
	int ret;

	ret = ln8000_update_reg(info, LN8000_REG_SYS_CTRL,
			    BIT(LN8000_BIT_REV_IIN_DET),
			    enable << LN8000_BIT_REV_IIN_DET);
	if (!ret)
		info->rcp_en = enable;

	return ret;
}

/* auto-recovery */
static int ln8000_enable_auto_recovery(struct ln8000_info *info, bool enable)
{
	return ln8000_update_reg(info, LN8000_REG_RECOVERY_CTRL, 0xF << 4,
				 ((0xF << 4) * enable));
}

static int ln8000_set_adc_mode(struct ln8000_info *info, unsigned int cfg)
{
	return ln8000_update_reg(info, LN8000_REG_ADC_CTRL, 0x7 << 5, cfg << 5);
}

static int ln8000_set_adc_hib_delay(struct ln8000_info *info, unsigned int cfg)
{
	return ln8000_update_reg(info, LN8000_REG_ADC_CTRL, 0x3 << 3, cfg << 3);
}

/* grab programmed battery float voltage (uV) */
static int ln8000_get_vbat_float(struct ln8000_info *info)
{
	unsigned int val;
	int ret;

	ret = ln8000_read_reg(info, LN8000_REG_V_FLOAT_CTRL, &val);
	if (ret < 0)
		return ret;

	return ((val & 0xFF) * LN8000_VBAT_FLOAT_LSB +
		LN8000_VBAT_FLOAT_MIN); //uV
}

/* grab programmed input current limit (uA) */
static int ln8000_get_iin_limit(struct ln8000_info *info)
{
	unsigned int val;
	int ret;
	int iin;

	ret = ln8000_read_reg(info, LN8000_REG_IIN_CTRL, &val);
	if (ret < 0)
		return ret;

	iin = ((val & 0x7F) * LN8000_IIN_CFG_LSB);

	if (iin < LN8000_IIN_CFG_MIN) {
		iin = LN8000_IIN_CFG_MIN;
	}

	return iin;
}

/* enable/disable STANDBY */
static inline void ln8000_sw_standby(struct ln8000_info *info, bool standby)
{
	u8 val = (standby) ? BIT(LN8000_BIT_STANDBY_EN) : 0x00;

	ln8000_update_reg(info, LN8000_REG_SYS_CTRL, BIT(LN8000_BIT_STANDBY_EN),
			  val);
}

/* Convert Raw ADC Code */
static void ln8000_convert_adc_code(struct ln8000_info *info, unsigned int ch,
				    u8 *sts, int *result)
{
	int adc_raw; // raw ADC value
	int adc_final; // final (converted) ADC value

	switch (ch) {
	case LN8000_ADC_CH_VOUT:
		adc_raw = ((sts[1] & 0xFF) << 2) | ((sts[0] & 0xC0) >> 6);
		adc_final = adc_raw * LN8000_ADC_VOUT_STEP; //uV
		break;
	case LN8000_ADC_CH_VIN:
		adc_raw = ((sts[1] & 0x3F) << 4) | ((sts[0] & 0xF0) >> 4);
		adc_final = adc_raw * LN8000_ADC_VIN_STEP; //uV
		break;
	case LN8000_ADC_CH_VBAT:
		adc_raw = ((sts[1] & 0x03) << 8) | (sts[0] & 0xFF);
		adc_final = adc_raw * LN8000_ADC_VBAT_STEP; //uV
		break;
	case LN8000_ADC_CH_VAC:
		adc_raw = (((sts[1] & 0x0F) << 6) | ((sts[0] & 0xFC) >> 2)) +
			  LN8000_ADC_VAC_OS;
		adc_final = adc_raw * LN8000_ADC_VAC_STEP; //uV
		break;
	case LN8000_ADC_CH_IIN:
		adc_raw = ((sts[1] & 0x03) << 8) | (sts[0] & 0xFF);
		adc_final = adc_raw * LN8000_ADC_IIN_STEP; //uA
		break;
	case LN8000_ADC_CH_DIETEMP:
		adc_raw = ((sts[1] & 0x0F) << 6) | ((sts[0] & 0xFC) >> 2);
		adc_final = (935 - adc_raw) * LN8000_ADC_DIETEMP_STEP /
			    LN8000_ADC_DIETEMP_DENOM; //dC
		if (adc_final > LN8000_ADC_DIETEMP_MAX)
			adc_final = LN8000_ADC_DIETEMP_MAX;
		else if (adc_final < LN8000_ADC_DIETEMP_MIN)
			adc_final = LN8000_ADC_DIETEMP_MIN;
		break;
	case LN8000_ADC_CH_TSBAT:
		adc_raw = ((sts[1] & 0x3F) << 4) | ((sts[0] & 0xF0) >> 4);
		adc_final = adc_raw * LN8000_ADC_NTCV_STEP; //(NTC) uV
		break;
	case LN8000_ADC_CH_TSBUS:
		adc_raw = ((sts[1] & 0xFF) << 2) | ((sts[0] & 0xC0) >> 6);
		adc_final = adc_raw * LN8000_ADC_NTCV_STEP; //(NTC) uV
		break;
	default:
		adc_raw = -EINVAL;
		adc_final = -EINVAL;
		break;
	}

	*result = adc_final;
	return;
}

static void ln8000_print_regmap(struct ln8000_info *info)
{
	const u8 print_reg_num =
		(LN8000_REG_CHARGE_CTRL - LN8000_REG_INT1_MSK) + 1;
	u32 regs[64] = {
		0x0,
	};
	char temp_buf[128] = {
		0,
	};
	int i, ret;

	for (i = 0; i < print_reg_num; ++i) {
		ret = ln8000_read_reg(info, LN8000_REG_INT1_MSK + i, &regs[i]);
		if (IS_ERR_VALUE((unsigned long)ret)) {
			ln_err("fail to read reg for print_regmap[%d]\n", i);
			regs[i] = 0xFF;
		}
		sprintf(temp_buf + strlen(temp_buf), "0x%02X[0x%02X],",
			LN8000_REG_INT1_MSK + i, regs[i]);
		if (((i + 1) % 10 == 0) || ((i + 1) == print_reg_num)) {
			ln_info("%s\n", temp_buf);
			memset(temp_buf, 0x0, sizeof(temp_buf));
		}
	}
}

/**
 * LN8000 device driver control routines
 */
static int ln8000_check_status(struct ln8000_info *info)
{
	u8 val[4];

	if (ln8000_bulk_read_reg(info, LN8000_REG_SYS_STS, val, 4) < 0) {
		return -EINVAL;
	}

	mutex_lock(&info->data_lock);

	info->vbat_regulated =
		LN8000_STATUS(val[0], LN8000_MASK_VFLOAT_LOOP_STS);
	info->iin_regulated = LN8000_STATUS(val[0], LN8000_MASK_IIN_LOOP_STS);
	info->pwr_status =
		val[0] &
		(LN8000_MASK_BYPASS_ENABLED | LN8000_MASK_SWITCHING_ENABLED |
		 LN8000_MASK_STANDBY_STS | LN8000_MASK_SHUTDOWN_STS);
	info->tdie_fault = LN8000_STATUS(val[1], LN8000_MASK_TEMP_MAX_STS);
	info->tdie_alarm =
		LN8000_STATUS(val[1], LN8000_MASK_TEMP_REGULATION_STS);
	info->tbus_tbat_fault =
		LN8000_STATUS(val[1], LN8000_MASK_NTC_SHUTDOWN_STS);
	info->tbus_tbat_alarm =
		LN8000_STATUS(val[1], LN8000_MASK_NTC_ALARM_STS);
	info->iin_rc = LN8000_STATUS(val[1], LN8000_MASK_REV_IIN_STS);

	info->wdt_fault = LN8000_STATUS(val[2], LN8000_MASK_WATCHDOG_TIMER_STS);
	info->vbat_ov = LN8000_STATUS(val[2], LN8000_MASK_VBAT_OV_STS);
	info->vac_unplug = LN8000_STATUS(val[2], LN8000_MASK_VAC_UNPLUG_STS);
	info->vac_ov = LN8000_STATUS(val[2], LN8000_MASK_VAC_OV_STS);
	info->vbus_ov = LN8000_STATUS(val[2], LN8000_MASK_VIN_OV_STS);
	info->volt_qual = !(LN8000_STATUS(val[2], 0x7F));
	if (info->volt_qual == 1 && info->chg_en == 1) {
		info->volt_qual = !(LN8000_STATUS(val[3], 1 << 5));
		if (info->volt_qual == 0) {
			ln_info("volt_fault_detected (volt_qual=%d)\n",
				info->volt_qual);
			/* clear latched status */
			ln8000_update_reg(info, LN8000_REG_TIMER_CTRL, 0x1 << 2,
					  0x1 << 2);
			ln8000_update_reg(info, LN8000_REG_TIMER_CTRL, 0x1 << 2,
					  0x0 << 2);
		}
	}
	info->iin_oc = LN8000_STATUS(val[3], LN8000_MASK_IIN_OC_DETECTED);

	mutex_unlock(&info->data_lock);

	return 0;
}

static int ln8000_update_opmode(struct ln8000_info *info)
{
	unsigned int op_mode;
	u32 val;
	int ret;

	ret = ln8000_read_reg(info, LN8000_REG_SYS_STS, &val);
	if (ret)
		return ret;

	if (val & LN8000_MASK_SHUTDOWN_STS) {
		op_mode = LN8000_OPMODE_STANDBY;
	} else if (val & LN8000_MASK_STANDBY_STS) {
		op_mode = LN8000_OPMODE_STANDBY;
	} else if (val & LN8000_MASK_SWITCHING_ENABLED) {
		op_mode = LN8000_OPMODE_SWITCHING;
	} else if (val & LN8000_MASK_BYPASS_ENABLED) {
		op_mode = LN8000_OPMODE_BYPASS;
	} else {
		op_mode = LN8000_OPMODE_UNKNOWN;
	}

	if (op_mode != info->op_mode) {
		/* IC already has been entered standby_mode, need to trigger standbt_en bit */
		if (op_mode == LN8000_OPMODE_STANDBY) {
			ln8000_update_reg(info, LN8000_REG_SYS_CTRL,
					  1 << LN8000_BIT_STANDBY_EN,
					  1 << LN8000_BIT_STANDBY_EN);
			ln_info("forced trigger standby_en\n");
			info->chg_en = 0;
		}
		ln_info("op_mode has been changed [%d]->[%d] (sys_st=0x%x)\n",
			info->op_mode, op_mode, val);
		info->op_mode = op_mode;
	}

	return 0;
}

static int ln8000_change_opmode(struct ln8000_info *info,
				unsigned int target_mode)
{
	int ret = 0;
	u8 val, msk = (0x1 << LN8000_BIT_STANDBY_EN |
		       0x1 << LN8000_BIT_EN_1TO1);

	switch (target_mode) {
	case LN8000_OPMODE_STANDBY:
		val = (1 << LN8000_BIT_STANDBY_EN);
		break;
	case LN8000_OPMODE_BYPASS:
		val = (0 << LN8000_BIT_STANDBY_EN) | (1 << LN8000_BIT_EN_1TO1);
		break;
	case LN8000_OPMODE_SWITCHING:
		val = (0 << LN8000_BIT_STANDBY_EN) | (0 << LN8000_BIT_EN_1TO1);
		break;
	default:
		ln_err("invalid index (target_mode=%d)\n", target_mode);
		return -EINVAL;
	}
	ret = ln8000_update_reg(info, LN8000_REG_SYS_CTRL, msk, val);
	if (ret)
		return ret;
	ln_info("changed opmode [%d] -> [%d]\n", info->op_mode, target_mode);
	info->op_mode = target_mode;

	return 0;
}

static int ln8000_init_device(struct ln8000_info *info)
{
	unsigned int vbat_float;
	int ret;

#define LN8000_INIT_STEP(_operation)                                        \
	do {                                                                \
		ret = (_operation);                                         \
		if (ret)                                                    \
			return dev_err_probe(info->dev, ret,                \
					     "initialization failed: %s\n", \
					     #_operation);                  \
	} while (0)

	/* The hardware OVP threshold is 102% of the float voltage. */
	vbat_float = rounddown(info->pdata->bat_ovp_th * 100 / 102, 1000);
	ln_info("bat_ovp_th=%d, vbat_float=%d\n", info->pdata->bat_ovp_th,
		vbat_float);
	LN8000_INIT_STEP(ln8000_set_vbat_float(info, vbat_float));
	info->vbat_ovp_alarm_th = info->pdata->bat_ovp_alarm_th;
	LN8000_INIT_STEP(ln8000_set_vac_ovp(info, info->pdata->bus_ovp_th));
	info->vin_ovp_alarm_th = info->pdata->bus_ovp_alarm_th;
	LN8000_INIT_STEP(
		ln8000_set_iin_limit(info, info->pdata->bus_ocp_th - 700000));
	info->iin_ocp_alarm_th = info->pdata->bus_ocp_alarm_th;
	LN8000_INIT_STEP(
		ln8000_set_ntc_alarm(info, info->pdata->ntc_alarm_cfg));

	LN8000_INIT_STEP(ln8000_update_reg(info, LN8000_REG_REGULATION_CTRL,
					   0x3 << 2, LN8000_NTC_SHUTDOWN_CFG));
	/* Faults stay latched until the driver has inspected them. */
	LN8000_INIT_STEP(ln8000_enable_auto_recovery(info, false));

	/* All hardware protections are mandatory for Nabu. */
	LN8000_INIT_STEP(ln8000_update_reg(
		info, LN8000_REG_FAULT_CTRL,
		BIT(LN8000_BIT_DISABLE_IIN_OCP) |
			BIT(LN8000_BIT_DISABLE_VBAT_OV) |
			BIT(LN8000_BIT_DISABLE_VAC_OV) |
			BIT(LN8000_BIT_DISABLE_VAC_UV) |
			BIT(LN8000_BIT_DISABLE_VIN_OV),
		0));
	LN8000_INIT_STEP(ln8000_enable_vbat_ovp(info, true));
	LN8000_INIT_STEP(ln8000_enable_vbat_regulation(info, true));
	LN8000_INIT_STEP(ln8000_enable_vbat_loop_int(info, true));
	LN8000_INIT_STEP(ln8000_enable_iin_ocp(info, true));
	LN8000_INIT_STEP(ln8000_enable_iin_regulation(info, true));
	LN8000_INIT_STEP(ln8000_enable_iin_loop_int(info, true));
	LN8000_INIT_STEP(ln8000_enable_tdie_regulation(info, true));
	LN8000_INIT_STEP(ln8000_enable_tdie_prot(info, true));
	LN8000_INIT_STEP(ln8000_enable_rcp(info, true));
	LN8000_INIT_STEP(ln8000_change_opmode(info, LN8000_OPMODE_STANDBY));
	LN8000_INIT_STEP(ln8000_enable_vac_ov(info, true));

	/* Keep the hardware watchdog and every populated ADC-backed protection active. */
	LN8000_INIT_STEP(ln8000_update_reg(info, LN8000_REG_TIMER_CTRL,
					   0x3 << 5,
					   LN8000_WATCHDOG_40SEC << 5));
	LN8000_INIT_STEP(ln8000_enable_wdt(info, true));
	LN8000_INIT_STEP(ln8000_set_adc_hib_delay(info, ADC_HIBERNATE_4S));
	LN8000_INIT_STEP(ln8000_set_adc_ch(info, LN8000_ADC_CH_ALL, true));
	if (info->pdata->input_ntc_unwired) {
		LN8000_INIT_STEP(ln8000_enable_tbus_monitor(info, false));
		LN8000_INIT_STEP(ln8000_set_adc_ch(info, LN8000_ADC_CH_TSBUS, false));
	} else {
		LN8000_INIT_STEP(ln8000_enable_tbus_monitor(info, true));
	}
	LN8000_INIT_STEP(ln8000_enable_tbat_monitor(info, true));
	LN8000_INIT_STEP(ln8000_set_adc_mode(info, ADC_AUTO_HIB_MODE));

	LN8000_INIT_STEP(ln8000_update_reg(info, LN8000_REG_CHARGE_CTRL, BIT(7),
					   BIT(7)));
	LN8000_INIT_STEP(
		ln8000_write_reg(info, LN8000_REG_THRESHOLD_CTRL, 0x0e));

	ln8000_print_regmap(info);
	ln_info("initialization complete; input NTC %s, all populated protections enabled\n",
		info->pdata->input_ntc_unwired ? "not populated" : "enabled");

#undef LN8000_INIT_STEP
	return 0;
}

/**
 * Support power_supply platform for charger block.
 * propertis are compatible by Xiaomi platform
 */
static int ln8000_get_adc_data(struct ln8000_info *info, unsigned int ch,
			       int *result)
{
	int ret;
	u8 sts[2];

	/* pause adc update */
	ret = ln8000_update_reg(info, LN8000_REG_TIMER_CTRL, 0x1 << 1,
				0x1 << 1);
	if (ret < 0) {
		ln_err("fail to update bit PAUSE_ADC_UPDATE:1 (ret=%d)\n", ret);
		return ret;
	}

	switch (ch) {
	case LN8000_ADC_CH_VOUT:
		ret = ln8000_bulk_read_reg(info, LN8000_REG_ADC04_STS, sts, 2);
		break;
	case LN8000_ADC_CH_VIN:
		ret = ln8000_bulk_read_reg(info, LN8000_REG_ADC03_STS, sts, 2);
		break;
	case LN8000_ADC_CH_VBAT:
		ret = ln8000_bulk_read_reg(info, LN8000_REG_ADC06_STS, sts, 2);
		break;
	case LN8000_ADC_CH_VAC:
		ret = ln8000_bulk_read_reg(info, LN8000_REG_ADC02_STS, sts, 2);
		break;
	case LN8000_ADC_CH_IIN:
		ret = ln8000_bulk_read_reg(info, LN8000_REG_ADC01_STS, sts, 2);
		break;
	case LN8000_ADC_CH_DIETEMP:
		ret = ln8000_bulk_read_reg(info, LN8000_REG_ADC07_STS, sts, 2);
		break;
	case LN8000_ADC_CH_TSBAT:
		ret = ln8000_bulk_read_reg(info, LN8000_REG_ADC08_STS, sts, 2);
		break;
	case LN8000_ADC_CH_TSBUS:
		ret = ln8000_bulk_read_reg(info, LN8000_REG_ADC09_STS, sts, 2);
		break;
	default:
		ln_err("invalid ch(%d)\n", ch);
		ret = -EINVAL;
		break;
	}

	/* resume adc update */
	ln8000_update_reg(info, LN8000_REG_TIMER_CTRL, 0x1 << 1, 0x0 << 1);

	if (IS_ERR_VALUE((unsigned long)ret) == false) {
		ln8000_convert_adc_code(info, ch, sts, result);
	}

	return ret;
}

static void psy_chg_get_ti_alarm_status(struct work_struct *work)
{
	struct ln8000_info *info =
		container_of(work, struct ln8000_info, charge_work.work);
	int v_offset;
	int ret;
	bool bus_ovp, bus_ocp, bat_ovp;
	u8 val[4];

	/* Rewriting the enable bit services the hardware watchdog. */
	ret = ln8000_enable_wdt(info, true);
	if (ret)
		goto protection_fault;
	ret = ln8000_check_status(info);
	if (ret)
		goto protection_fault;
	ret = ln8000_get_adc_data(info, LN8000_ADC_CH_VIN, &info->vbus_uV);
	if (ret)
		goto protection_fault;
	ret = ln8000_get_adc_data(info, LN8000_ADC_CH_IIN, &info->iin_uA);
	if (ret)
		goto protection_fault;
	ret = ln8000_get_adc_data(info, LN8000_ADC_CH_VBAT, &info->vbat_uV);
	if (ret)
		goto protection_fault;

	bus_ovp = (info->vbus_uV > info->vin_ovp_alarm_th) ? 1 : 0;
	bus_ocp = (info->iin_uA > info->iin_ocp_alarm_th) ? 1 : 0;
	bat_ovp = (info->vbat_uV > info->vbat_ovp_alarm_th) ? 1 : 0;

	if (bus_ovp || bus_ocp || bat_ovp || info->tdie_fault ||
	    info->tbus_tbat_fault || info->wdt_fault || info->vbat_ov ||
	    info->vac_ov || info->vbus_ov || info->iin_oc || info->iin_rc ||
	    info->vac_unplug || !info->volt_qual)
		goto protection_fault;

	v_offset = info->vbus_uV - (info->vbat_uV * 2);
	/* after charging-enabled, When the input current rises above rcp_th(over 200mA), it activates rcp. */
	if (info->chg_en && !(info->rcp_en)) {
		if (info->iin_uA > 200000 && v_offset > 300000) {
			ln8000_enable_rcp(info, 1);
			ln_info("enabled rcp\n");
		}
	}
	/* If an unplug event occurs when vbus voltage lower then vin_start_up_th, switch to standby mode. */
	if (info->chg_en && !(info->rcp_en)) {
		if (v_offset < 100000) {
			ln8000_change_opmode(info, LN8000_OPMODE_STANDBY);
			ln_info("forced change standby_mode for prevent reverse current\n");
			info->chg_en = 0;
		}
	}

	ln8000_bulk_read_reg(info, LN8000_REG_SYS_STS, val, 4);
	ln_info("adc_vin=%d(th=%d), adc_iin=%d(th=%d), adc_vbat=%d(th=%d), v_offset=%d\n",
		info->vbus_uV / 1000, info->vin_ovp_alarm_th / 1000,
		info->iin_uA / 1000, info->iin_ocp_alarm_th / 1000,
		info->vbat_uV / 1000, info->vbat_ovp_alarm_th / 1000,
		v_offset / 1000);
	ln_info("st:0x%x:0x%x:0x%x:0x%x\n", val[0], val[1], val[2], val[3]);

	if (info->chg_en)
		schedule_delayed_work(&info->charge_work,
				      msecs_to_jiffies(1000));

	return;

protection_fault:
	ln_err("protection or telemetry fault (%d); forcing standby\n", ret);
	ret = psy_chg_set_charging_enable(info, false);
	if (ret)
		dev_err(info->dev, "failed to restore safe charger state: %d\n",
			ret);
	power_supply_changed(info->psy_chg);
}

static int ln8000_charger_get_property(struct power_supply *psy,
				       enum power_supply_property prop,
				       union power_supply_propval *val)
{
	struct ln8000_info *info = power_supply_get_drvdata(psy);

	switch (prop) {
	case POWER_SUPPLY_PROP_STATUS:
		ln8000_check_status(info);
		val->intval = info->chg_en ? POWER_SUPPLY_STATUS_CHARGING :
					     POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = info->usb_present;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = info->chg_en;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		ln8000_check_status(info);
		if (info->tdie_fault || info->tbus_tbat_fault)
			val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
		else if (info->vbat_ov || info->vac_ov || info->vbus_ov)
			val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		else if (info->iin_oc)
			val->intval = POWER_SUPPLY_HEALTH_OVERCURRENT;
		else if (info->wdt_fault)
			val->intval = POWER_SUPPLY_HEALTH_WATCHDOG_TIMER_EXPIRE;
		else
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ln8000_get_adc_data(info, LN8000_ADC_CH_VIN, &info->vbus_uV);
		val->intval = info->vbus_uV;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ln8000_get_adc_data(info, LN8000_ADC_CH_IIN, &info->iin_uA);
		val->intval = info->iin_uA;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		ln8000_get_adc_data(info, LN8000_ADC_CH_DIETEMP,
				    &info->tdie_dC);
		val->intval = info->tdie_dC;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		val->intval = ln8000_get_vbat_float(info);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		val->intval = ln8000_get_iin_limit(info);
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = "ln8000";
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = "Lion Semiconductor";
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ln8000_set_switching_charger(struct ln8000_info *info, bool enable)
{
	union power_supply_propval value = { .intval = enable };
	int ret;

	ret = power_supply_set_property(info->switching_psy,
					POWER_SUPPLY_PROP_STATUS, &value);
	if (!ret)
		info->switching_charger_suspended = !enable;

	return ret;
}

static int psy_chg_set_charging_enable(struct ln8000_info *info, int val)
{
	int op_mode;
	int ret;
	int restore_ret;

	if (val) {
		ln_info("start charging\n");
		op_mode = LN8000_OPMODE_SWITCHING;
		ret = ln8000_set_switching_charger(info, false);
		if (ret)
			return dev_err_probe(info->dev, ret,
					     "failed to suspend switching charger\n");
	} else {
		ln_info("stop charging\n");
		op_mode = LN8000_OPMODE_STANDBY;
	}

	ret = ln8000_change_opmode(info, op_mode);
	if (ret)
		goto restore_switching;

	usleep_range(10000, 12000);
	ret = ln8000_update_opmode(info);
	if (ret)
		goto restore_switching;

	ln8000_print_regmap(info);
	info->chg_en = info->op_mode == LN8000_OPMODE_SWITCHING;

	ln_info("op_mode=%d\n", info->op_mode);

	ret = info->op_mode == op_mode ? 0 : -EIO;

restore_switching:
	if ((!val || ret) && info->switching_charger_suspended) {
		restore_ret = ln8000_set_switching_charger(info, true);
		if (!ret)
			ret = restore_ret;
	}

	return ret;
}

static enum power_supply_property ln8000_charger_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

/**
 * Support IRQ interface
 */
static int ln8000_read_int_value(struct ln8000_info *info, u32 *reg_val)
{
	int ret;

	/* pause INT updates */
	ln8000_update_reg(info, LN8000_REG_TIMER_CTRL, 0x1, 0x1);
	mdelay(1);

	ret = ln8000_read_reg(info, LN8000_REG_INT1, reg_val);

	/* resume INT updates */
	ln8000_update_reg(info, LN8000_REG_TIMER_CTRL, 0x1, 0x0);

	return ret;
}

static irqreturn_t ln8000_interrupt_handler(int irq, void *data)
{
	struct ln8000_info *info = data;
	u32 int_reg, int_msk;
	u8 masked_int;
	int ret;

	ln_err("ln8000_interrupt_handler enter!\n");
	ret = ln8000_read_int_value(info, &int_reg);
	if (IS_ERR_VALUE((unsigned long)ret)) {
		ln_err("fail to read INT reg (ret=%d)\n", ret);
		goto fail_closed;
	}
	ret = ln8000_read_reg(info, LN8000_REG_INT1_MSK, &int_msk);
	if (ret)
		goto fail_closed;
	masked_int = int_reg & ~int_msk;

	ln_info("int_reg=0x%x, int_msk=0x%x, masked_int=0x%x\n", int_reg,
		int_msk, masked_int);

	ln8000_print_regmap(info);
	LN8000_BIT_CHECK(masked_int, 7, "(INT) FAULT_INT");
	LN8000_BIT_CHECK(masked_int, 6, "(INT) NTC_PROT_INT");
	LN8000_BIT_CHECK(masked_int, 5, "(INT) CHARGE_PHASE_INT");
	LN8000_BIT_CHECK(masked_int, 4, "(INT) MODE_INT");
	LN8000_BIT_CHECK(masked_int, 3, "(INT) REV_CURR_INT");
	LN8000_BIT_CHECK(masked_int, 2, "(INT) TEMP_INT");
	LN8000_BIT_CHECK(masked_int, 1, "(INT) ADC_DONE_INT");
	LN8000_BIT_CHECK(masked_int, 0, "(INT) TIMER_INT");
	ret = ln8000_check_status(info);
	if (ret)
		goto fail_closed;
	if (info->vbat_ov || info->vac_ov || info->vbus_ov || info->iin_oc ||
	    info->iin_rc || info->tdie_fault || info->tbus_tbat_fault ||
	    info->wdt_fault) {
		psy_chg_set_charging_enable(info, false);
		power_supply_changed(info->psy_chg);
	}

	if (masked_int & LN8000_MASK_FAULT_INT) { /* FAULT_INT */
		if (info->volt_qual) {
			ln_info("connected to power_supplier\n");
		} else {
			ln_info("FAULT_INT has occurred\n");
		}
	}
	if (masked_int & LN8000_MASK_NTC_PROT_INT) { /* NTC_PROT_INT */
		ln_info("NTC_PROT_INT has occurred(ntc_fault=%d, ntc_alarm=%d)\n",
			info->tbus_tbat_fault, info->tbus_tbat_alarm);
	}
	if (masked_int & LN8000_MASK_CHARGE_PHASE_INT) { /* CHARGE_PHASE_INT */
		if (info->vbat_regulated) {
			ln_info("CHARGE_PHASE_INT: VFLOAT regulated\n");
		} else if (info->iin_regulated) {
			ln_info("CHARGE_PHASE_INT: IIN regulated\n");
		}
	}
	if (masked_int & LN8000_MASK_MODE_INT) { /* MODE_INT */
		switch (info->pwr_status) {
		case LN8000_MASK_BYPASS_ENABLED:
			ln_info("MODE_INT: device in BYPASS mode\n");
			break;
		case LN8000_MASK_SWITCHING_ENABLED:
			ln_info("MODE_INT: device in SWITCHING mode\n");
			break;
		case LN8000_MASK_STANDBY_STS:
			ln_info("MODE_INT: device in STANDBY mode\n");
			break;
		case LN8000_MASK_SHUTDOWN_STS:
			ln_info("MODE_INT: device in SHUTDOWN mode\n");
			break;
		default:
			ln_info("MODE_INT: device in  unknown mode\n");
			break;
		}
	}
	if (masked_int & LN8000_MASK_TEMP_INT) { /* TEMP_INT */
		ln_info("TEMP_INT has occurred(tdie_fault=%d, tdie_alarm=%d)\n",
			info->tdie_fault, info->tdie_alarm);
	}
	if (masked_int & LN8000_MASK_TIMER_INT) { /* TIMER_INT */
		ln_info("Watchdog timer has expired(wdt_fault=%d)\n",
			info->wdt_fault);
	}

	return IRQ_HANDLED;

fail_closed:
	ln_err("interrupt telemetry failure; forcing standby\n");
	psy_chg_set_charging_enable(info, false);
	power_supply_changed(info->psy_chg);

	return IRQ_HANDLED;
}

static int ln8000_irq_init(struct ln8000_info *info)
{
	const struct ln8000_platform_data *pdata = info->pdata;
	int ret;
	u8 mask;
	u32 int_reg;

	if (info->pdata->irq_gpio) {
		info->client->irq = gpiod_to_irq(pdata->irq_gpio);
		if (info->client->irq < 0)
			return dev_err_probe(info->dev, info->client->irq,
					     "failed to map IRQ GPIO\n");
		ln_info("mapped GPIO to irq (%d)\n", info->client->irq);
	}

	/* interrupt mask setting */
	mask = LN8000_MASK_ADC_DONE_INT | LN8000_MASK_MODE_INT;
	/* Only noisy completion interrupts are masked; all fault paths stay live. */
	ret = ln8000_write_reg(info, LN8000_REG_INT1_MSK, mask);
	if (ret)
		return ret;
	/* read clear int_reg */
	ret = ln8000_read_int_value(info, &int_reg);
	if (ret) {
		ln_err("fail to read INT reg (ret=%d)\n", ret);
		return ret;
	}
	ln_info("int1_msk=0x%x\n", mask);

	return 0;
}

static void determine_initial_status(struct ln8000_info *info)
{
	if (info->client->irq)
		ln8000_interrupt_handler(info->client->irq, info);
}

static const struct of_device_id ln8000_dt_match[] = {
	{ .compatible = "lionsemi,ln8000" },
	{},
};
MODULE_DEVICE_TABLE(of, ln8000_dt_match);

static const struct regmap_config ln8000_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = LN8000_REG_MAX,
};

static int ln8000_parse_dt(struct ln8000_info *info)
{
	struct device *dev = info->dev;
	struct ln8000_platform_data *pdata = info->pdata;
	int ret;

	if (!info->client->irq) {
		pdata->irq_gpio = devm_gpiod_get_optional(dev, "irq", GPIOD_IN);
		if (IS_ERR(pdata->irq_gpio))
			return dev_err_probe(dev, PTR_ERR(pdata->irq_gpio),
					     "failed to acquire IRQ GPIO\n");
	}

	ret = device_property_read_u32(dev, "lionsemi,battery-ovp-microvolt",
				       &pdata->bat_ovp_th);
	if (ret)
		return dev_err_probe(dev, ret, "missing battery OVP threshold\n");
	ret = device_property_read_u32(
		dev, "lionsemi,battery-ovp-alarm-microvolt",
		&pdata->bat_ovp_alarm_th);
	if (ret)
		return dev_err_probe(dev, ret, "missing battery OVP alarm\n");
	ret = device_property_read_u32(dev, "lionsemi,input-ovp-microvolt",
				       &pdata->bus_ovp_th);
	if (ret)
		return dev_err_probe(dev, ret, "missing input OVP threshold\n");
	ret = device_property_read_u32(
		dev, "lionsemi,input-ovp-alarm-microvolt",
		&pdata->bus_ovp_alarm_th);
	if (ret)
		return dev_err_probe(dev, ret, "missing input OVP alarm\n");
	ret = device_property_read_u32(dev, "lionsemi,input-ocp-microamp",
				       &pdata->bus_ocp_th);
	if (ret)
		return dev_err_probe(dev, ret, "missing input OCP threshold\n");
	ret = device_property_read_u32(dev,
				       "lionsemi,input-ocp-alarm-microamp",
				       &pdata->bus_ocp_alarm_th);
	if (ret)
		return dev_err_probe(dev, ret, "missing input OCP alarm\n");
	ret = device_property_read_u32(dev, "lionsemi,ntc-alarm-code",
				       &pdata->ntc_alarm_cfg);
	if (ret)
		return dev_err_probe(dev, ret, "missing NTC alarm threshold\n");
	ret = device_property_read_u32(dev, "lionsemi,min-input-microvolt",
				       &pdata->min_input_uv);
	if (ret)
		return dev_err_probe(dev, ret, "missing minimum input voltage\n");
	pdata->input_ntc_unwired =
		device_property_read_bool(dev, "lionsemi,input-ntc-unwired");

	if (pdata->bat_ovp_alarm_th >= pdata->bat_ovp_th ||
	    pdata->bus_ovp_alarm_th >= pdata->bus_ovp_th ||
	    pdata->bus_ocp_alarm_th >= pdata->bus_ocp_th ||
	    pdata->min_input_uv >= pdata->bus_ovp_alarm_th)
		return dev_err_probe(dev, -EINVAL,
				     "unsafe or inconsistent protection thresholds\n");

	return 0;
}

static int ln8000_psy_register(struct ln8000_info *info)
{
	info->psy_cfg.drv_data = info;
	info->psy_cfg.fwnode = dev_fwnode(info->dev);
	info->psy_desc.name = "ln8000-charger";
	info->psy_desc.type = POWER_SUPPLY_TYPE_MAINS;
	info->psy_desc.properties = ln8000_charger_props;
	info->psy_desc.num_properties = ARRAY_SIZE(ln8000_charger_props);
	info->psy_desc.get_property = ln8000_charger_get_property;
	info->psy_chg = devm_power_supply_register(
		&info->client->dev, &info->psy_desc, &info->psy_cfg);
	if (IS_ERR(info->psy_chg)) {
		ln_err("failed to register power supply\n");
		return PTR_ERR(info->psy_chg);
	}

	ln_info("successfully registered power supply\n");

	return 0;
}

static void ln8000_status_changed_worker(struct work_struct *work)
{
	struct ln8000_info *chip = container_of(work, struct ln8000_info,
						status_changed_work.work);
	union power_supply_propval online = {};
	union power_supply_propval voltage = {};
	bool enable = false;
	int ret;

	ret = power_supply_get_property(chip->typec_psy,
					POWER_SUPPLY_PROP_ONLINE, &online);
	if (ret)
		goto out_disable;

	chip->status = online.intval;
	chip->usb_present = online.intval != 0;
	if (!chip->usb_present)
		goto out_disable;

	ret = power_supply_get_property(
		chip->typec_psy, POWER_SUPPLY_PROP_VOLTAGE_NOW, &voltage);
	if (ret || voltage.intval < chip->pdata->min_input_uv)
		goto out_disable;

	ret = ln8000_check_status(chip);
	if (ret)
		goto out_disable;

	enable = chip->volt_qual && !chip->vbat_ov && !chip->vac_ov &&
		 !chip->vbus_ov && !chip->iin_oc && !chip->tdie_fault &&
		 !chip->tbus_tbat_fault && !chip->wdt_fault;

out_disable:
	if (enable != chip->chg_en ||
	    (!enable && chip->switching_charger_suspended)) {
		ret = psy_chg_set_charging_enable(chip, enable);
		if (ret) {
			dev_err(chip->dev, "failed to enter %s mode: %d\n",
				enable ? "switching" : "standby", ret);
			enable = false;
			psy_chg_set_charging_enable(chip, false);
		}
		if (enable)
			mod_delayed_work(system_wq, &chip->charge_work, 0);
		else
			cancel_delayed_work(&chip->charge_work);
	}

	power_supply_changed(chip->psy_chg);
}

static int ln8000_notifier_call(struct notifier_block *nb, unsigned long val,
				void *v)
{
	struct ln8000_info *chip = container_of(nb, struct ln8000_info, nb);
	struct power_supply *psy = v;

	if (psy == chip->typec_psy && !READ_ONCE(chip->suspended))
		mod_delayed_work(system_wq, &chip->status_changed_work,
				 msecs_to_jiffies(100));

	return NOTIFY_OK;
}

static int ln8000_probe(struct i2c_client *client)
{
	struct ln8000_info *info;
	int ret = 0;

	/* detect device on connected i2c bus */
	ret = i2c_smbus_read_byte_data(client, LN8000_REG_DEVICE_ID);
	if (ret < 0)
		return dev_err_probe(&client->dev, ret,
				     "failed to read device ID at 0x%02x\n",
				     client->addr);
	if (ret != LN8000_DEVICE_ID)
		return dev_err_probe(&client->dev, -ENODEV,
				     "unexpected device ID 0x%02x\n", ret);
	dev_info(&client->dev, "device id=0x%x\n", ret);

	info = devm_kzalloc(&client->dev, sizeof(struct ln8000_info),
			    GFP_KERNEL);
	if (info == NULL) {
		dev_err(&client->dev,
			"%s: fail to alloc devm for ln8000_info\n", __func__);
		return -ENOMEM;
	}
	info->dev = &client->dev;
	info->client = client;

	info->typec_psy = devm_power_supply_get_by_reference(
		info->dev, "input-power-supply");
	if (IS_ERR(info->typec_psy))
		return dev_err_probe(info->dev, PTR_ERR(info->typec_psy),
				     "failed to get TCPM power supply\n");
	if (!info->typec_psy)
		return dev_err_probe(info->dev, -EPROBE_DEFER,
				     "TCPM power supply is not ready\n");
	info->switching_psy = devm_power_supply_get_by_reference(
		info->dev, "switching-power-supply");
	if (IS_ERR(info->switching_psy))
		return dev_err_probe(info->dev, PTR_ERR(info->switching_psy),
				     "failed to get PM8150B charger supply\n");
	if (!info->switching_psy)
		return dev_err_probe(info->dev, -EPROBE_DEFER,
				     "PM8150B charger supply is not ready\n");

	info->pdata = devm_kzalloc(
		&client->dev, sizeof(struct ln8000_platform_data), GFP_KERNEL);
	if (info->pdata == NULL) {
		ln_err("fail to alloc devm for ln8000_platform_data\n");
		return -ENOMEM;
	}
	ret = ln8000_parse_dt(info);
	if (IS_ERR_VALUE((unsigned long)ret)) {
		ln_err("fail to parsed dt\n");
		return ret;
	}

	info->regmap = devm_regmap_init_i2c(client, &ln8000_regmap_config);
	if (IS_ERR(info->regmap)) {
		ln_err("fail to initialize regmap\n");
		return PTR_ERR(info->regmap);
	}

	mutex_init(&info->data_lock);
	mutex_init(&info->i2c_lock);
	mutex_init(&info->irq_lock);
	i2c_set_clientdata(client, info);
	INIT_DELAYED_WORK(&info->status_changed_work,
			  ln8000_status_changed_worker);
	INIT_DELAYED_WORK(&info->charge_work, psy_chg_get_ti_alarm_status);

	ret = ln8000_init_device(info);
	if (ret)
		return ret;

	ret = ln8000_psy_register(info);
	if (ret)
		return ret;

	ret = ln8000_irq_init(info);
	if (ret < 0)
		return ret;

	if (client->irq) {
		ret = devm_request_threaded_irq(&client->dev, client->irq, NULL,
						ln8000_interrupt_handler,
						IRQF_TRIGGER_FALLING |
							IRQF_ONESHOT,
						"ln8000-charger-irq", info);
		if (ret < 0) {
			return dev_err_probe(info->dev, ret,
					     "failed to request IRQ %d\n",
					     client->irq);
		}
	} else {
		ln_info("don't support isr(irq=%d)\n", info->client->irq);
	}

	device_init_wakeup(info->dev, 1);

	determine_initial_status(info);

	info->nb.notifier_call = ln8000_notifier_call;
	ret = power_supply_reg_notifier(&info->nb);
	if (ret) {
		device_init_wakeup(info->dev, false);
		return dev_err_probe(
			info->dev, ret,
			"failed to register power-supply notifier\n");
	}
	info->notifier_registered = true;
	mod_delayed_work(system_wq, &info->status_changed_work, 0);

	return 0;
}

static void ln8000_remove(struct i2c_client *client)
{
	struct ln8000_info *info = i2c_get_clientdata(client);

	if (info->notifier_registered)
		power_supply_unreg_notifier(&info->nb);
	cancel_delayed_work_sync(&info->status_changed_work);
	cancel_delayed_work_sync(&info->charge_work);
	psy_chg_set_charging_enable(info, false);
	if (info->irq_wake_enabled)
		disable_irq_wake(client->irq);
	device_init_wakeup(info->dev, false);
}

static void ln8000_shutdown(struct i2c_client *client)
{
	struct ln8000_info *info = i2c_get_clientdata(client);

	WRITE_ONCE(info->suspended, true);
	cancel_delayed_work_sync(&info->status_changed_work);
	cancel_delayed_work_sync(&info->charge_work);
	psy_chg_set_charging_enable(info, false);
}

#if defined(CONFIG_PM)
static int ln8000_suspend(struct device *dev)
{
	struct ln8000_info *info = dev_get_drvdata(dev);
	int ret;

	/* The watchdog cannot be serviced while the SoC is asleep. */
	WRITE_ONCE(info->suspended, true);
	cancel_delayed_work_sync(&info->status_changed_work);
	cancel_delayed_work_sync(&info->charge_work);
	ret = psy_chg_set_charging_enable(info, false);
	if (ret) {
		WRITE_ONCE(info->suspended, false);
		mod_delayed_work(system_wq, &info->status_changed_work, 0);
		return dev_err_probe(dev, ret,
				     "failed to enter safe suspend state\n");
	}

	if (device_may_wakeup(dev) && info->client->irq &&
	    !info->irq_wake_enabled) {
		ret = enable_irq_wake(info->client->irq);
		if (ret)
			return ret;
		info->irq_wake_enabled = true;
	}

	return 0;
}

static int ln8000_resume(struct device *dev)
{
	struct ln8000_info *info = dev_get_drvdata(dev);

	if (info->irq_wake_enabled) {
		disable_irq_wake(info->client->irq);
		info->irq_wake_enabled = false;
	}
	WRITE_ONCE(info->suspended, false);
	mod_delayed_work(system_wq, &info->status_changed_work, 0);

	return 0;
}

static const struct dev_pm_ops ln8000_pm_ops = {
	.suspend = ln8000_suspend,
	.resume = ln8000_resume,
};
#endif

static struct i2c_driver ln8000_driver = {
	.driver = {
		.name = "ln8000_charger",
		.of_match_table = of_match_ptr(ln8000_dt_match),
#if defined(CONFIG_PM)
		.pm = &ln8000_pm_ops,
#endif
	},
	.probe = ln8000_probe,
	.remove = ln8000_remove,
	.shutdown = ln8000_shutdown,
};
module_i2c_driver(ln8000_driver);

MODULE_AUTHOR("sungdae choi<sungdae@lionsemi.com>");
MODULE_DESCRIPTION("LIONSEMI LN8000 charger driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.3.0");
