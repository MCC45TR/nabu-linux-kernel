// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Linaro Ltd.
 * Author: Casey Connolly <casey.connolly@linaro.org>
 *
 * This driver is for the switch-mode battery charger and boost
 * hardware found in pmi8998 and related PMICs.
 */

#include <linux/bits.h>
#include <linux/devm-helpers.h>
#include <linux/iio/consumer.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/string.h>
#include <linux/thermal.h>
#include <linux/types.h>
#include <linux/workqueue.h>

enum smb_generation {
	SMB2,
	SMB5,
};

#define SMB_REG_OFFSET(smb) (smb->gen == SMB2 ? 0x600 : 0x100)

/* clang-format off */
#define BATTERY_CHARGER_STATUS_1			0x06
#define BATTERY_CHARGER_STATUS_MASK			GENMASK(2, 0)

#define BATTERY_CHARGER_STATUS_2			0x07
#define SMB2_CHARGER_ERROR_STATUS_BAT_OV_BIT		BIT(5)
#define SMB2_BAT_TEMP_STATUS_HOT_SOFT_LIMIT_BIT	BIT(3)
#define SMB2_BAT_TEMP_STATUS_COLD_SOFT_LIMIT_BIT	BIT(2)
#define SMB2_BAT_TEMP_STATUS_TOO_HOT_BIT		BIT(1)
#define SMB5_CHARGER_ERROR_STATUS_BAT_OV_BIT		BIT(1)
#define SMB2_BAT_TEMP_STATUS_TOO_COLD_BIT		BIT(0)

#define BATTERY_CHARGER_STATUS_7			0x0D
#define SMB5_BAT_TEMP_STATUS_HOT_SOFT_BIT		BIT(5)
#define SMB5_BAT_TEMP_STATUS_COLD_SOFT_BIT		BIT(4)
#define SMB5_BAT_TEMP_STATUS_TOO_HOT_BIT		BIT(3)
#define SMB5_BAT_TEMP_STATUS_TOO_COLD_BIT		BIT(2)

#define CHARGING_ENABLE_CMD				0x42
#define CHARGING_ENABLE_CMD_BIT				BIT(0)

#define CHGR_CFG2					0x51
#define CHG_EN_SRC_BIT					BIT(7)
#define CHG_EN_POLARITY_BIT				BIT(6)
#define PRETOFAST_TRANSITION_CFG_BIT			BIT(5)
#define BAT_OV_ECC_BIT					BIT(4)
#define I_TERM_BIT					BIT(3)
#define AUTO_RECHG_BIT					BIT(2)
#define EN_ANALOG_DROP_IN_VBATT_BIT			BIT(1)
#define CHARGER_INHIBIT_BIT				BIT(0)

#define PRE_CHARGE_CURRENT_CFG				0x60
#define PRE_CHARGE_CURRENT_SETTING_MASK			GENMASK(5, 0)

#define FAST_CHARGE_CURRENT_CFG				0x61
#define FAST_CHARGE_CURRENT_SETTING_MASK		GENMASK(7, 0)

#define FLOAT_VOLTAGE_CFG				0x70
#define FLOAT_VOLTAGE_SETTING_MASK			GENMASK(7, 0)

#define SMB2_FG_UPDATE_CFG_2_SEL			0x7D
#define SMB2_SOC_LT_CHG_RECHARGE_THRESH_SEL_BIT		BIT(2)
#define SMB2_VBT_LT_CHG_RECHARGE_THRESH_SEL_BIT		BIT(1)

#define SMB5_CHARGE_RCHG_SOC_THRESHOLD_CFG_REG		0x7D
#define SMB5_CHARGE_RCHG_SOC_THRESHOLD_CFG_MASK		GENMASK(7, 0)

#define OTG_CFG						0x153
#define OTG_EN_SRC_CFG_BIT				BIT(1)

#define APSD_STATUS					0x307
#define APSD_DTC_STATUS_DONE_BIT			BIT(0)

#define APSD_RESULT_STATUS				0x308
#define APSD_RESULT_STATUS_MASK				GENMASK(6, 0)
#define FLOAT_CHARGER_BIT				BIT(4)
#define DCP_CHARGER_BIT					BIT(3)
#define CDP_CHARGER_BIT					BIT(2)
#define OCP_CHARGER_BIT					BIT(1)
#define SDP_CHARGER_BIT					BIT(0)

#define USBIN_CMD_IL					0x340
#define USBIN_SUSPEND_BIT				BIT(0)

#define CMD_APSD					0x341
#define APSD_RERUN_BIT					BIT(0)

#define CMD_ICL_OVERRIDE				0x342
#define ICL_OVERRIDE_BIT				BIT(0)

#define TYPE_C_CFG					0x358
#define APSD_START_ON_CC_BIT				BIT(7)
#define FACTORY_MODE_DETECTION_EN_BIT			BIT(5)
#define VCONN_OC_CFG_BIT				BIT(1)

#define USBIN_OPTIONS_1_CFG				0x362
#define AUTO_SRC_DETECT_BIT				BIT(3)
#define HVDCP_EN_BIT					BIT(2)

#define USBIN_LOAD_CFG					0x65
#define ICL_OVERRIDE_AFTER_APSD_BIT			BIT(4)

#define USBIN_ICL_OPTIONS				0x366
#define USB51_MODE_BIT					BIT(1)
#define USBIN_MODE_CHG_BIT				BIT(0)

/* PMI8998 only */
#define TYPE_C_INTRPT_ENB_SOFTWARE_CTRL			0x368
#define SMB2_VCONN_EN_SRC_BIT				BIT(4)
#define VCONN_EN_VALUE_BIT				BIT(3)
#define TYPEC_POWER_ROLE_CMD_MASK			GENMASK(2, 0)
#define SMB5_EN_SNK_ONLY_BIT				BIT(1)

#define USBIN_CURRENT_LIMIT_CFG				0x370

#define USBIN_AICL_OPTIONS_CFG				0x380
#define SUSPEND_ON_COLLAPSE_USBIN_BIT			BIT(7)
#define USBIN_AICL_START_AT_MAX_BIT			BIT(5)
#define USBIN_AICL_PERIODIC_RERUN_EN_BIT		BIT(4)
#define USBIN_AICL_ADC_EN_BIT				BIT(3)
#define USBIN_AICL_EN_BIT				BIT(2)
#define USBIN_HV_COLLAPSE_RESPONSE_BIT			BIT(1)
#define USBIN_LV_COLLAPSE_RESPONSE_BIT			BIT(0)

// FIXME: drop these and their programming, no need to set min to 4.3v
#define USBIN_5V_AICL_THRESHOLD_CFG			0x381
#define USBIN_5V_AICL_THRESHOLD_CFG_MASK		GENMASK(2, 0)

#define USBIN_CONT_AICL_THRESHOLD_CFG			0x384
#define USBIN_CONT_AICL_THRESHOLD_CFG_MASK		GENMASK(5, 0)

#define ICL_STATUS(smb)					(SMB_REG_OFFSET(smb) + 0x07)
#define INPUT_CURRENT_LIMIT_MASK			GENMASK(7, 0)

#define POWER_PATH_STATUS(smb)				(SMB_REG_OFFSET(smb) + 0x0B)
#define P_PATH_USE_USBIN_BIT				BIT(4)
#define P_PATH_VALID_INPUT_POWER_SOURCE_STS_BIT		BIT(0)

/* 0x5xx region is PM8150b only Type-C registers */

/* Bits 2:0 match PMI8998 TYPE_C_INTRPT_ENB_SOFTWARE_CTRL */
#define SMB5_TYPE_C_MODE_CFG				0x544
#define SMB5_EN_TRY_SNK_BIT				BIT(4)
#define SMB5_EN_SNK_ONLY_BIT				BIT(1)

#define SMB5_TYPEC_TYPE_C_VCONN_CONTROL			0x546
#define SMB5_VCONN_EN_ORIENTATION_BIT			BIT(2)
#define SMB5_VCONN_EN_VALUE_BIT				BIT(1)
#define SMB5_VCONN_EN_SRC_BIT				BIT(0)


#define SMB5_TYPE_C_DEBUG_ACCESS_SINK			0x54a
#define SMB5_TYPEC_DEBUG_ACCESS_SINK_MASK		GENMASK(4, 0)

#define SMB5_DEBUG_ACCESS_SRC_CFG			0x54C
#define SMB5_EN_UNORIENTED_DEBUG_ACCESS_SRC_BIT	BIT(0)

#define SMB5_TYPE_C_EXIT_STATE_CFG			0x550
#define SMB5_BYPASS_VSAFE0V_DURING_ROLE_SWAP_BIT	BIT(3)
#define SMB5_SEL_SRC_UPPER_REF_BIT			BIT(2)
#define SMB5_EXIT_SNK_BASED_ON_CC_BIT			BIT(0)

/* Common */

#define BARK_BITE_WDOG_PET				0x643
#define BARK_BITE_WDOG_PET_BIT				BIT(0)

#define WD_CFG						0x651
#define WATCHDOG_TRIGGER_AFP_EN_BIT			BIT(7)
#define BARK_WDOG_INT_EN_BIT				BIT(6)
#define WDOG_TIMER_EN_ON_PLUGIN_BIT			BIT(1)

#define SNARL_BARK_BITE_WD_CFG				0x653

#define AICL_RERUN_TIME_CFG				0x661
#define AICL_RERUN_TIME_MASK				GENMASK(1, 0)
#define AIC_RERUN_TIME_3_SECS				0x0

/* FIXME: probably remove this so we get parallel charging? */
#define STAT_CFG					0x690
#define STAT_SW_OVERRIDE_CFG_BIT			BIT(6)

#define SDP_CURRENT_UA					500000
#define CDP_CURRENT_UA					1500000
#define DCP_CURRENT_UA					1500000
#define CURRENT_MAX_UA					DCP_CURRENT_UA

#define SMB5_FAST_CHARGE_CURRENT_UA			1950000
/* Conservative current used until the complete safety policy succeeds. */
#define SMB5_THERMAL_LIMIT_CURRENT_UA			1000000
#define SMB5_THERMAL_POLL_MS				1000
#define SMB5_JEITA_HYSTERESIS_DECIC			20
#define SMB5_PD_5P9V_UV					5900000
#define SMB5_PD_6P5V_UV					6500000
#define SMB5_PD_7P5V_UV					7500000
#define SMB5_PD_8P5V_UV					8500000
#define SMB5_CONNECTOR_SHUTDOWN_MC			70000
#define SMB5_CONNECTOR_RESTART_MC			60000

/* pmi8998 registers represent current in increments of 1/40th of an amp */
#define CURRENT_SCALE_FACTOR				25000
/* clang-format on */

enum charger_status {
	TRICKLE_CHARGE = 0,
	PRE_CHARGE,
	FAST_CHARGE,
	FULLON_CHARGE,
	TAPER_CHARGE,
	TERMINATE_CHARGE,
	INHIBIT_CHARGE,
	DISABLE_CHARGE,
};

struct smb_jeita_range {
	s32 temp_min_decic;
	s32 temp_max_decic;
	u32 current_ua;
	u32 voltage_uv;
};

struct smb_init_register {
	u16 addr;
	u8 mask;
	u8 val;
};

/**
 * struct smb_chip - smb chip structure
 * @dev:		Device reference for power_supply
 * @name:		The platform device name
 * @base:		Base address for smb registers
 * @regmap:		Register map
 * @batt_info:		Battery data from DT
 * @status_change_work: Worker to handle plug/unplug events
 * @thermal_work:	Worker to enforce the SMB5 hardware JEITA state
 * @jeita_ranges:	Battery-profile temperature/current/voltage ranges
 * @num_jeita_ranges: Number of software JEITA ranges
 * @jeita_index:	Active software JEITA range, or -1
 * @jeita_blocked:	Charging is latched off beyond a hard JEITA boundary
 * @policy_health:	Combined hardware and software JEITA health
 * @policy_lock:	Serialises charge-limit calculation and register writes
 * @thermal_levels:	Fast-charge limits for cooling-device states
 * @thermal_input_levels: Input-current limits for cooling-device states
 * @thermal_dcp_levels: DCP input-current limits for cooling-device states
 * @thermal_pd_levels: PD base input-current limits for cooling-device states
 * @num_thermal_levels: Number of thermal mitigation levels
 * @thermal_cooling_state: State requested by the thermal framework
 * @user_cooling_state: Additional userspace-requested cooling state
 * @source_current_ua:	Input-current ceiling detected for the USB source
 * @source_voltage_uv:	Negotiated USB source voltage
 * @usb_type:		Detected TCPM/APSD USB source type
 * @user_fcc_ua:	Optional userspace fast-charge-current ceiling
 * @user_icl_ua:	Optional userspace input-current ceiling
 * @applied_fcc_ua:	Last fast-charge current programmed to hardware
 * @applied_icl_ua:	Last input-current limit programmed to hardware
 * @charging_enabled:	Last charging-enable state programmed to hardware
 * @cdev:		Linux thermal cooling device
 * @input_psy:		Optional TCPM power supply describing the PD contract
 * @nb:			Battery and TCPM power-supply change notifier
 * @connector_temp_chan: USB connector thermistor temperature channel
 * @connector_overheat: Connector shutdown latch with restart hysteresis
 * @cable_irq:		USB plugin IRQ
 * @wakeup_enabled:	If the cable IRQ will cause a wakeup
 * @usb_in_i_chan:	USB_IN current measurement channel
 * @usb_in_v_chan:	USB_IN voltage measurement channel
 * @chg_psy:		Charger power supply instance
 * @current_step_ua:	Current-register resolution for this PMIC generation
 * @fv_min_uv:		Voltage represented by float-voltage register value zero
 * @fv_max_uv:		Maximum representable float voltage
 * @fv_step_uv:		Float-voltage register resolution
 */
struct smb_chip {
	struct device *dev;
	const char *name;
	unsigned int base;
	struct regmap *regmap;
	struct power_supply_battery_info *batt_info;
	enum smb_generation gen;

	struct delayed_work status_change_work;
	struct delayed_work thermal_work;
	struct smb_jeita_range *jeita_ranges;
	unsigned int num_jeita_ranges;
	int jeita_index;
	bool jeita_blocked;
	int policy_health;
	struct mutex policy_lock;
	u32 *thermal_levels;
	u32 *thermal_input_levels;
	u32 *thermal_dcp_levels;
	u32 *thermal_pd_levels;
	unsigned int num_thermal_levels;
	unsigned long thermal_cooling_state;
	unsigned long user_cooling_state;
	unsigned int source_current_ua;
	unsigned int source_voltage_uv;
	int usb_type;
	unsigned int user_fcc_ua;
	unsigned int user_icl_ua;
	unsigned int applied_fcc_ua;
	unsigned int applied_icl_ua;
	bool charging_enabled;
	struct thermal_cooling_device *cdev;
	struct power_supply *input_psy;
	struct notifier_block nb;
	struct iio_channel *connector_temp_chan;
	bool connector_overheat;
	int cable_irq;
	bool wakeup_enabled;

	struct iio_channel *usb_in_i_chan;
	struct iio_channel *usb_in_v_chan;

	struct power_supply *chg_psy;
	unsigned int current_step_ua;
	unsigned int fv_min_uv;
	unsigned int fv_max_uv;
	unsigned int fv_step_uv;
};

struct smb_match_data {
	const char *name;
	enum smb_generation gen;
	size_t init_seq_len;
	const struct smb_init_register *init_seq;
	unsigned int current_step_ua;
	unsigned int fv_min_uv;
	unsigned int fv_max_uv;
	unsigned int fv_step_uv;
};

static enum power_supply_property smb_properties[] = {
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_USB_TYPE,
};

static int smb_get_prop_usb_online(struct smb_chip *chip, int *val)
{
	unsigned int stat;
	int rc;

	rc = regmap_read(chip->regmap, chip->base + POWER_PATH_STATUS(chip), &stat);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't read power path status: %d\n", rc);
		return rc;
	}

	*val = (stat & P_PATH_USE_USBIN_BIT) &&
	       (stat & P_PATH_VALID_INPUT_POWER_SOURCE_STS_BIT);
	return 0;
}

/*
 * Qualcomm "automatic power source detection" aka APSD
 * tells us what type of charger we're connected to.
 */
static int smb_apsd_get_charger_type(struct smb_chip *chip, int *val)
{
	unsigned int apsd_stat, stat;
	int usb_online = 0;
	int rc;

	rc = smb_get_prop_usb_online(chip, &usb_online);
	if (!usb_online) {
		*val = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		return rc;
	}

	rc = regmap_read(chip->regmap, chip->base + APSD_STATUS, &apsd_stat);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to read apsd status, rc = %d", rc);
		return rc;
	}
	if (!(apsd_stat & APSD_DTC_STATUS_DONE_BIT)) {
		dev_dbg(chip->dev, "Apsd not ready");
		return -EAGAIN;
	}

	rc = regmap_read(chip->regmap, chip->base + APSD_RESULT_STATUS, &stat);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to read apsd result, rc = %d", rc);
		return rc;
	}

	stat &= APSD_RESULT_STATUS_MASK;

	if (stat & CDP_CHARGER_BIT)
		*val = POWER_SUPPLY_USB_TYPE_CDP;
	else if (stat & (DCP_CHARGER_BIT | OCP_CHARGER_BIT | FLOAT_CHARGER_BIT))
		*val = POWER_SUPPLY_USB_TYPE_DCP;
	else /* SDP_CHARGER_BIT (or others) */
		*val = POWER_SUPPLY_USB_TYPE_SDP;

	return 0;
}

/* Return 1 when in overvoltage state, else 0 or -errno */
static int smbx_ov_status(struct smb_chip *chip)
{
	u16 reg;
	u8 mask;
	int rc;
	u32 val;

	switch (chip->gen) {
	case SMB2:
		reg = BATTERY_CHARGER_STATUS_2;
		mask = SMB2_CHARGER_ERROR_STATUS_BAT_OV_BIT;
		break;
	case SMB5:
		reg = BATTERY_CHARGER_STATUS_7;
		mask = SMB5_CHARGER_ERROR_STATUS_BAT_OV_BIT;
		break;
	}

	rc = regmap_read(chip->regmap, chip->base + reg, &val);
	if (rc)
		return rc;

	return !!(val & mask);
}

static int smb_get_prop_status(struct smb_chip *chip, int *val)
{
	u32 stat;
	int usb_online = 0;
	int rc;

	rc = smb_get_prop_usb_online(chip, &usb_online);
	if (!usb_online) {
		*val = POWER_SUPPLY_STATUS_DISCHARGING;
		return rc;
	}

	rc = regmap_read(chip->regmap,
			      chip->base + BATTERY_CHARGER_STATUS_1, &stat);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to read charging status ret=%d\n",
			rc);
		return rc;
	}

	rc = smbx_ov_status(chip);
	if (rc < 0)
		return rc;

	/* In overvoltage state */
	if (rc == 1) {
		*val = POWER_SUPPLY_STATUS_NOT_CHARGING;
		return 0;
	}

	stat = stat & BATTERY_CHARGER_STATUS_MASK;

	switch (stat) {
	case TRICKLE_CHARGE:
	case PRE_CHARGE:
	case FAST_CHARGE:
	case FULLON_CHARGE:
	case TAPER_CHARGE:
		*val = POWER_SUPPLY_STATUS_CHARGING;
		return rc;
	case DISABLE_CHARGE:
		*val = POWER_SUPPLY_STATUS_NOT_CHARGING;
		return rc;
	case TERMINATE_CHARGE:
	case INHIBIT_CHARGE:
		*val = POWER_SUPPLY_STATUS_FULL;
		return rc;
	default:
		*val = POWER_SUPPLY_STATUS_UNKNOWN;
		return rc;
	}
}

static inline int smb_get_current_limit(struct smb_chip *chip,
					 unsigned int *val)
{
	int rc = regmap_read(chip->regmap, chip->base + ICL_STATUS(chip), val);

	if (rc >= 0)
		*val *= chip->current_step_ua;
	return rc;
}

static int smb_set_current_limit(struct smb_chip *chip, unsigned int val)
{
	unsigned char val_raw;

	if (val > 4800000) {
		dev_err(chip->dev,
			"Can't set current limit higher than 4800000uA");
		return -EINVAL;
	}
	val_raw = val / chip->current_step_ua;

	return regmap_write(chip->regmap, chip->base + USBIN_CURRENT_LIMIT_CFG,
			    val_raw);
}

static void smb_update_input_contract(struct smb_chip *chip)
{
	union power_supply_propval val;
	unsigned int current_ua = SDP_CURRENT_UA;
	unsigned int voltage_uv = 5000000;
	int apsd_type, usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;

	if (!chip->input_psy)
		return;
	if (power_supply_get_property(chip->input_psy,
				      POWER_SUPPLY_PROP_ONLINE, &val) ||
	    !val.intval)
		goto update;

	if (!power_supply_get_property(chip->input_psy,
					       POWER_SUPPLY_PROP_CURRENT_MAX, &val) &&
	    val.intval > 0)
		current_ua = min_t(unsigned int, val.intval,
					   chip->batt_info->constant_charge_current_max_ua);

	if (!power_supply_get_property(chip->input_psy,
					       POWER_SUPPLY_PROP_VOLTAGE_NOW, &val) &&
	    val.intval > 0)
		voltage_uv = val.intval;

	if (!power_supply_get_property(chip->input_psy,
				       POWER_SUPPLY_PROP_USB_TYPE, &val) &&
	    (val.intval == POWER_SUPPLY_USB_TYPE_PD ||
	     val.intval == POWER_SUPPLY_USB_TYPE_PD_DRP ||
	     val.intval == POWER_SUPPLY_USB_TYPE_PD_PPS))
		usb_type = val.intval;
	else if (!smb_apsd_get_charger_type(chip, &apsd_type))
		usb_type = apsd_type;

update:
	WRITE_ONCE(chip->source_current_ua, current_ua);
	WRITE_ONCE(chip->source_voltage_uv, voltage_uv);
	WRITE_ONCE(chip->usb_type, usb_type);
}

static void smb_status_change_work(struct work_struct *work)
{
	unsigned int charger_type, current_ua;
	int usb_online = 0;
	int count, rc;
	struct smb_chip *chip;

	chip = container_of(work, struct smb_chip, status_change_work.work);

	smb_get_prop_usb_online(chip, &usb_online);
	if (!usb_online) {
		if (chip->gen == SMB5) {
			WRITE_ONCE(chip->source_current_ua, SDP_CURRENT_UA);
			WRITE_ONCE(chip->source_voltage_uv, 5000000);
			WRITE_ONCE(chip->usb_type,
				   POWER_SUPPLY_USB_TYPE_UNKNOWN);
			mod_delayed_work(system_wq, &chip->thermal_work, 0);
		}
		return;
	}

	for (count = 0; count < 3; count++) {
		dev_dbg(chip->dev, "get charger type retry %d\n", count);
		rc = smb_apsd_get_charger_type(chip, &charger_type);
		if (rc != -EAGAIN)
			break;
		msleep(100);
	}

	if (rc < 0 && rc != -EAGAIN) {
		dev_err(chip->dev, "get charger type failed: %d\n", rc);
		return;
	}

	if (rc < 0) {
		rc = regmap_update_bits(chip->regmap, chip->base + CMD_APSD,
					APSD_RERUN_BIT, APSD_RERUN_BIT);
		schedule_delayed_work(&chip->status_change_work,
				      msecs_to_jiffies(1000));
		dev_dbg(chip->dev, "get charger type failed, rerun apsd\n");
		return;
	}

	switch (charger_type) {
	case POWER_SUPPLY_USB_TYPE_CDP:
		current_ua = CDP_CURRENT_UA;
		break;
	case POWER_SUPPLY_USB_TYPE_DCP:
		current_ua = chip->batt_info->constant_charge_current_max_ua;
		break;
	case POWER_SUPPLY_USB_TYPE_SDP:
	default:
		current_ua = SDP_CURRENT_UA;
		break;
	}

	if (chip->gen == SMB5) {
		WRITE_ONCE(chip->usb_type, charger_type);
		WRITE_ONCE(chip->source_current_ua, current_ua);
		smb_update_input_contract(chip);
		mod_delayed_work(system_wq, &chip->thermal_work, 0);
	} else {
		smb_set_current_limit(chip, current_ua);
	}
	power_supply_changed(chip->chg_psy);
}

static int smb_get_iio_chan(struct smb_chip *chip, struct iio_channel *chan,
			     int *val)
{
	int rc;
	union power_supply_propval status;

	rc = power_supply_get_property(chip->chg_psy, POWER_SUPPLY_PROP_STATUS,
				       &status);
	if (rc < 0 || status.intval != POWER_SUPPLY_STATUS_CHARGING) {
		*val = 0;
		return 0;
	}

	if (IS_ERR(chan)) {
		dev_err(chip->dev, "Failed to chan, err = %li", PTR_ERR(chan));
		return PTR_ERR(chan);
	}

	return iio_read_channel_processed(chan, val);
}

static int smb5_get_prop_health(struct smb_chip *chip, int *val)
{
	int rc;
	unsigned int stat;

	rc = smbx_ov_status(chip);

	/* Treat any error as if we are in the overvoltage state */
	if (rc < 0)
		dev_err(chip->dev, "Couldn't determine overvoltage status!");
	if (rc) {
		dev_err(chip->dev, "battery over-voltage");
		*val = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		return 0;
	}

	rc = regmap_read(chip->regmap, chip->base + BATTERY_CHARGER_STATUS_7,
			 &stat);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't read charger status 7 rc=%d\n", rc);
		return rc;
	}

	if (stat & SMB5_BAT_TEMP_STATUS_TOO_COLD_BIT)
		*val = POWER_SUPPLY_HEALTH_COLD;
	else if (stat & SMB5_BAT_TEMP_STATUS_TOO_HOT_BIT)
		*val = POWER_SUPPLY_HEALTH_OVERHEAT;
	else if (stat & SMB5_BAT_TEMP_STATUS_COLD_SOFT_BIT)
		*val = POWER_SUPPLY_HEALTH_COOL;
	else if (stat & SMB5_BAT_TEMP_STATUS_HOT_SOFT_BIT)
		*val = POWER_SUPPLY_HEALTH_WARM;
	else
		*val = POWER_SUPPLY_HEALTH_GOOD;

	return 0;
}

static int smb2_get_prop_health(struct smb_chip *chip, int *val)
{
	int rc;
	unsigned int stat;

	rc = regmap_read(chip->regmap, chip->base + BATTERY_CHARGER_STATUS_2,
			 &stat);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't read charger status rc=%d\n", rc);
		return rc;
	}

	switch (stat) {
	case SMB2_CHARGER_ERROR_STATUS_BAT_OV_BIT:
		*val = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		return 0;
	case SMB2_BAT_TEMP_STATUS_TOO_COLD_BIT:
		*val = POWER_SUPPLY_HEALTH_COLD;
		return 0;
	case SMB2_BAT_TEMP_STATUS_TOO_HOT_BIT:
		*val = POWER_SUPPLY_HEALTH_OVERHEAT;
		return 0;
	case SMB2_BAT_TEMP_STATUS_COLD_SOFT_LIMIT_BIT:
		*val = POWER_SUPPLY_HEALTH_COOL;
		return 0;
	case SMB2_BAT_TEMP_STATUS_HOT_SOFT_LIMIT_BIT:
		*val = POWER_SUPPLY_HEALTH_WARM;
		return 0;
	default:
		*val = POWER_SUPPLY_HEALTH_GOOD;
		return 0;
	}
}

static int smb_get_prop_health(struct smb_chip *chip, int *val)
{
	switch (chip->gen) {
	case SMB2:
		return smb2_get_prop_health(chip, val);
	case SMB5:
		return smb5_get_prop_health(chip, val);
	default:
		dev_err(chip->dev, "Unsupported SMB chip generation\n");
		return -EINVAL;
	}
}

static int smb_set_fast_charge_current(struct smb_chip *chip,
				       unsigned int current_ua)
{
	if (current_ua > FAST_CHARGE_CURRENT_SETTING_MASK *
				 chip->current_step_ua)
		return -EINVAL;

	return regmap_write(chip->regmap,
			    chip->base + FAST_CHARGE_CURRENT_CFG,
			    current_ua / chip->current_step_ua);
}

static int smb_get_fast_charge_current(struct smb_chip *chip, int *current_ua)
{
	unsigned int val;
	int rc;

	rc = regmap_read(chip->regmap,
			 chip->base + FAST_CHARGE_CURRENT_CFG, &val);
	if (rc < 0)
		return rc;

	*current_ua = (val & FAST_CHARGE_CURRENT_SETTING_MASK) *
			      chip->current_step_ua;

	return 0;
}

static int smb_set_float_voltage(struct smb_chip *chip, unsigned int voltage_uv)
{
	unsigned int val;

	if (voltage_uv < chip->fv_min_uv || voltage_uv > chip->fv_max_uv)
		return -EINVAL;

	/* Round down so the programmed voltage never exceeds the ceiling. */
	val = (voltage_uv - chip->fv_min_uv) / chip->fv_step_uv;

	return regmap_update_bits(chip->regmap,
				  chip->base + FLOAT_VOLTAGE_CFG,
				  FLOAT_VOLTAGE_SETTING_MASK, val);
}

static int smb_set_charging_enabled(struct smb_chip *chip, bool enable)
{
	return regmap_update_bits(chip->regmap,
				  chip->base + CHARGING_ENABLE_CMD,
				  CHARGING_ENABLE_CMD_BIT,
				  enable ? CHARGING_ENABLE_CMD_BIT : 0);
}

static int smb_read_battery_temperature(int *temp_decic)
{
	union power_supply_propval val;
	struct power_supply *battery;
	int rc;

	battery = power_supply_get_by_name("qcom-battery");
	if (!battery)
		return -ENODEV;

	rc = power_supply_get_property(battery, POWER_SUPPLY_PROP_TEMP, &val);
	power_supply_put(battery);
	if (rc < 0)
		return rc;

	*temp_decic = val.intval;
	return 0;
}

static int smb_find_jeita_range(struct smb_chip *chip, int temp_decic)
{
	struct smb_jeita_range *active, *candidate;
	unsigned int last = chip->num_jeita_ranges - 1;
	int candidate_index = -ERANGE;
	unsigned int i;

	for (i = 0; i < chip->num_jeita_ranges; i++) {
		candidate = &chip->jeita_ranges[i];
		if (temp_decic >= candidate->temp_min_decic &&
		    temp_decic <= candidate->temp_max_decic) {
			candidate_index = i;
			break;
		}
	}
	if (candidate_index < 0) {
		chip->jeita_index = temp_decic < chip->jeita_ranges[0].temp_min_decic ?
				    0 : last;
		chip->jeita_blocked = true;
		return -ERANGE;
	}

	if (chip->jeita_blocked) {
		if ((chip->jeita_index == 0 && candidate_index == 0 &&
		     temp_decic < chip->jeita_ranges[0].temp_min_decic +
				  SMB5_JEITA_HYSTERESIS_DECIC) ||
		    (chip->jeita_index == last && candidate_index == last &&
		     temp_decic > chip->jeita_ranges[last].temp_max_decic -
				  SMB5_JEITA_HYSTERESIS_DECIC))
			return -ERANGE;
		chip->jeita_blocked = false;
	}

	if (chip->jeita_index < 0 ||
	    candidate_index == chip->jeita_index)
		return candidate_index;

	active = &chip->jeita_ranges[chip->jeita_index];
	candidate = &chip->jeita_ranges[candidate_index];

	/* Tighten immediately; use hysteresis only when relaxing a limit. */
	if (candidate->current_ua <= active->current_ua &&
	    candidate->voltage_uv <= active->voltage_uv)
		return candidate_index;

	if (candidate_index > chip->jeita_index &&
	    temp_decic <= active->temp_max_decic +
			  SMB5_JEITA_HYSTERESIS_DECIC)
		return chip->jeita_index;
	if (candidate_index < chip->jeita_index &&
	    temp_decic >= active->temp_min_decic -
			  SMB5_JEITA_HYSTERESIS_DECIC)
		return chip->jeita_index;

	return candidate_index;
}

static int smb_get_software_jeita(struct smb_chip *chip, int *health,
				  unsigned int *current_ua,
				  unsigned int *voltage_uv)
{
	struct smb_jeita_range *range;
	int index, temp_decic, rc;
	int alert_min, alert_max, hard_min, hard_max;

	if (!chip->num_jeita_ranges)
		return -EOPNOTSUPP;

	rc = smb_read_battery_temperature(&temp_decic);
	if (rc < 0)
		return rc;

	index = smb_find_jeita_range(chip, temp_decic);
	if (index < 0) {
		/* Preserve which hard boundary owns the restart hysteresis. */
		*health = chip->jeita_index == 0 ?
			  POWER_SUPPLY_HEALTH_COLD :
			  POWER_SUPPLY_HEALTH_OVERHEAT;
		*current_ua = 0;
		*voltage_uv = 0;
		return 0;
	}

	chip->jeita_index = index;
	range = &chip->jeita_ranges[index];
	*current_ua = range->current_ua;
	*voltage_uv = range->voltage_uv;

	hard_min = chip->batt_info->temp_min;
	hard_max = chip->batt_info->temp_max;
	alert_min = chip->batt_info->temp_alert_min;
	alert_max = chip->batt_info->temp_alert_max;

	if (hard_min > INT_MIN && temp_decic < hard_min * 10)
		*health = POWER_SUPPLY_HEALTH_COLD;
	else if (hard_max < INT_MAX && temp_decic > hard_max * 10)
		*health = POWER_SUPPLY_HEALTH_OVERHEAT;
	else if (alert_min > INT_MIN && temp_decic < alert_min * 10)
		*health = POWER_SUPPLY_HEALTH_COOL;
	else if (alert_max < INT_MAX && temp_decic > alert_max * 10)
		*health = POWER_SUPPLY_HEALTH_WARM;
	else
		*health = POWER_SUPPLY_HEALTH_GOOD;

	return 0;
}

static int smb_merge_health(int hardware, int software)
{
	if (hardware == POWER_SUPPLY_HEALTH_OVERVOLTAGE ||
	    software == POWER_SUPPLY_HEALTH_OVERVOLTAGE)
		return POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	if (hardware == POWER_SUPPLY_HEALTH_OVERHEAT ||
	    software == POWER_SUPPLY_HEALTH_OVERHEAT)
		return POWER_SUPPLY_HEALTH_OVERHEAT;
	if (hardware == POWER_SUPPLY_HEALTH_COLD ||
	    software == POWER_SUPPLY_HEALTH_COLD)
		return POWER_SUPPLY_HEALTH_COLD;
	if ((hardware != POWER_SUPPLY_HEALTH_GOOD &&
	     hardware != POWER_SUPPLY_HEALTH_WARM &&
	     hardware != POWER_SUPPLY_HEALTH_COOL) ||
	    (software != POWER_SUPPLY_HEALTH_GOOD &&
	     software != POWER_SUPPLY_HEALTH_WARM &&
	     software != POWER_SUPPLY_HEALTH_COOL))
		return POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
	if ((hardware == POWER_SUPPLY_HEALTH_WARM &&
	     software == POWER_SUPPLY_HEALTH_COOL) ||
	    (hardware == POWER_SUPPLY_HEALTH_COOL &&
	     software == POWER_SUPPLY_HEALTH_WARM))
		return POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
	if (hardware == POWER_SUPPLY_HEALTH_WARM ||
	    software == POWER_SUPPLY_HEALTH_WARM)
		return POWER_SUPPLY_HEALTH_WARM;
	if (hardware == POWER_SUPPLY_HEALTH_COOL ||
	    software == POWER_SUPPLY_HEALTH_COOL)
		return POWER_SUPPLY_HEALTH_COOL;

	return POWER_SUPPLY_HEALTH_GOOD;
}

static int smb_get_connector_health(struct smb_chip *chip, int *health)
{
	int temp_mc, rc;

	if (!chip->connector_temp_chan) {
		*health = POWER_SUPPLY_HEALTH_GOOD;
		return 0;
	}

	rc = iio_read_channel_processed(chip->connector_temp_chan, &temp_mc);
	if (rc < 0)
		return rc;

	if (temp_mc >= SMB5_CONNECTOR_SHUTDOWN_MC)
		chip->connector_overheat = true;
	else if (temp_mc <= SMB5_CONNECTOR_RESTART_MC)
		chip->connector_overheat = false;

	*health = chip->connector_overheat ? POWER_SUPPLY_HEALTH_OVERHEAT :
						 POWER_SUPPLY_HEALTH_GOOD;
	return 0;
}

static unsigned int smb_get_thermal_input_limit(struct smb_chip *chip,
						 unsigned long state)
{
	unsigned int limit = chip->thermal_input_levels[state];
	unsigned int voltage_uv = READ_ONCE(chip->source_voltage_uv);
	unsigned int contract_limit;
	int usb_type = READ_ONCE(chip->usb_type);

	if (usb_type == POWER_SUPPLY_USB_TYPE_DCP && chip->thermal_dcp_levels)
		return min(limit, chip->thermal_dcp_levels[state]);

	if (usb_type != POWER_SUPPLY_USB_TYPE_PD &&
	    usb_type != POWER_SUPPLY_USB_TYPE_PD_DRP &&
	    usb_type != POWER_SUPPLY_USB_TYPE_PD_PPS)
		return limit;
	if (!chip->thermal_pd_levels)
		return limit;

	contract_limit = chip->thermal_pd_levels[state];
	if (voltage_uv >= SMB5_PD_8P5V_UV)
		contract_limit = contract_limit * 65 / 100;
	else if (voltage_uv >= SMB5_PD_7P5V_UV)
		contract_limit = contract_limit * 70 / 100;
	else if (voltage_uv >= SMB5_PD_6P5V_UV)
		contract_limit = contract_limit * 75 / 100;
	else if (voltage_uv >= SMB5_PD_5P9V_UV)
		contract_limit = contract_limit * 85 / 100;

	return min(limit, contract_limit);
}

static int smb_apply_charge_policy(struct smb_chip *chip, int health,
				   unsigned int jeita_current_ua,
				   unsigned int jeita_voltage_uv)
{
	unsigned int current_ua, input_ua, voltage_uv;
	unsigned long cooling_state;
	bool changed = false, enable;
	int rc;

	current_ua = min_t(unsigned int, SMB5_FAST_CHARGE_CURRENT_UA,
			   chip->batt_info->constant_charge_current_max_ua);
	voltage_uv = chip->batt_info->constant_charge_voltage_max_uv;
	if ((int)voltage_uv <= 0)
		voltage_uv = chip->batt_info->voltage_max_design_uv;
	input_ua = READ_ONCE(chip->source_current_ua) ?: SDP_CURRENT_UA;
	if (READ_ONCE(chip->user_fcc_ua))
		current_ua = min(current_ua, READ_ONCE(chip->user_fcc_ua));
	if (READ_ONCE(chip->user_icl_ua))
		input_ua = min(input_ua, READ_ONCE(chip->user_icl_ua));

	if (jeita_current_ua)
		current_ua = min(current_ua, jeita_current_ua);
	if (jeita_voltage_uv)
		voltage_uv = min(voltage_uv, jeita_voltage_uv);

	cooling_state = max(READ_ONCE(chip->thermal_cooling_state),
			    READ_ONCE(chip->user_cooling_state));
	cooling_state = min_t(unsigned long, cooling_state,
			      chip->num_thermal_levels - 1);
	current_ua = min(current_ua, chip->thermal_levels[cooling_state]);
	input_ua = min(input_ua,
		       smb_get_thermal_input_limit(chip, cooling_state));

	enable = health == POWER_SUPPLY_HEALTH_GOOD ||
		 health == POWER_SUPPLY_HEALTH_WARM ||
		 health == POWER_SUPPLY_HEALTH_COOL;
	if (chip->num_thermal_levels > 1 &&
	    cooling_state == chip->num_thermal_levels - 1)
		enable = false;
	if (health == POWER_SUPPLY_HEALTH_WARM ||
	    health == POWER_SUPPLY_HEALTH_COOL)
		current_ua = min(current_ua,
				 SMB5_THERMAL_LIMIT_CURRENT_UA);

	mutex_lock(&chip->policy_lock);
	if (enable) {
		/* Apply all ceilings before enabling or increasing charge power. */
		rc = smb_set_current_limit(chip, input_ua);
		if (rc < 0)
			goto disable_on_error;
		rc = smb_set_float_voltage(chip, voltage_uv);
		if (rc < 0)
			goto disable_on_error;
		rc = smb_set_fast_charge_current(chip, current_ua);
		if (rc < 0)
			goto disable_on_error;
		rc = smb_set_charging_enabled(chip, true);
		if (rc < 0)
			goto disable_on_error;
	} else {
		rc = smb_set_charging_enabled(chip, false);
		if (rc < 0)
			goto out_unlock;
	}

	changed = chip->applied_fcc_ua != current_ua ||
		  chip->applied_icl_ua != input_ua ||
		  chip->charging_enabled != enable ||
		  chip->policy_health != health;
	if (changed)
		dev_info(chip->dev,
			 "charge policy health=%d cooling=%lu fcc=%u icl=%u enabled=%d\n",
			 health, cooling_state, current_ua, input_ua, enable);

	chip->applied_fcc_ua = current_ua;
	chip->applied_icl_ua = input_ua;
	chip->charging_enabled = enable;
	chip->policy_health = health;
	rc = 0;
	goto out_unlock;

disable_on_error:
	/* A partially applied limit set must never leave charging enabled. */
	if (!smb_set_charging_enabled(chip, false))
		chip->charging_enabled = false;

out_unlock:
	mutex_unlock(&chip->policy_lock);
	if (!rc && changed)
		power_supply_changed(chip->chg_psy);

	return rc;
}

static void smb_thermal_work(struct work_struct *work)
{
	struct smb_chip *chip =
		container_of(work, struct smb_chip, thermal_work.work);
	unsigned int jeita_current_ua = UINT_MAX;
	unsigned int jeita_voltage_uv = UINT_MAX;
	int hardware_health, software_health, connector_health, health, rc;

	smb_update_input_contract(chip);
	rc = smb_get_prop_health(chip, &hardware_health);
	if (rc < 0) {
		health = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
	} else {
		rc = smb_get_software_jeita(chip, &software_health,
					    &jeita_current_ua,
					    &jeita_voltage_uv);
		if (rc == -EOPNOTSUPP) {
			software_health = POWER_SUPPLY_HEALTH_GOOD;
			jeita_current_ua = UINT_MAX;
			jeita_voltage_uv = UINT_MAX;
		} else if (rc < 0) {
			software_health = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
			jeita_current_ua = 0;
			jeita_voltage_uv = 0;
		}
		health = smb_merge_health(hardware_health, software_health);
		rc = smb_get_connector_health(chip, &connector_health);
		if (rc < 0)
			connector_health = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		health = smb_merge_health(health, connector_health);
	}

	rc = smb_apply_charge_policy(chip, health, jeita_current_ua,
				     jeita_voltage_uv);
	if (rc < 0)
		dev_err_ratelimited(chip->dev,
				    "failed to apply charge policy: %d\n", rc);

	mod_delayed_work(system_wq, &chip->thermal_work,
			 msecs_to_jiffies(SMB5_THERMAL_POLL_MS));
}

static int smb_get_property(struct power_supply *psy,
			     enum power_supply_property psp,
			     union power_supply_propval *val)
{
	struct smb_chip *chip = power_supply_get_drvdata(psy);
	int policy_health, ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = "Qualcomm";
		return 0;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = chip->name;
		return 0;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		return smb_get_current_limit(chip, &val->intval);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		return smb_get_fast_charge_current(chip, &val->intval);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = chip->batt_info->constant_charge_current_max_ua;
		return 0;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		val->intval = max(READ_ONCE(chip->thermal_cooling_state),
				  READ_ONCE(chip->user_cooling_state));
		return 0;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
		val->intval = chip->num_thermal_levels ?
			      chip->num_thermal_levels - 1 : 0;
		return 0;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		return smb_get_iio_chan(chip, chip->usb_in_i_chan,
					 &val->intval);
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = smb_get_iio_chan(chip, chip->usb_in_v_chan,
					 &val->intval);
		if (!ret) {
			if (chip->gen == SMB5)
				val->intval *= 16;
		}
		return ret;
	case POWER_SUPPLY_PROP_ONLINE:
		return smb_get_prop_usb_online(chip, &val->intval);
	case POWER_SUPPLY_PROP_STATUS:
		return smb_get_prop_status(chip, &val->intval);
	case POWER_SUPPLY_PROP_HEALTH:
		ret = smb_get_prop_health(chip, &val->intval);
		if (ret < 0)
			return ret;
		policy_health = READ_ONCE(chip->policy_health);
		if (policy_health != POWER_SUPPLY_HEALTH_UNKNOWN)
			val->intval = smb_merge_health(val->intval, policy_health);
		return 0;
	case POWER_SUPPLY_PROP_USB_TYPE:
		return smb_apsd_get_charger_type(chip, &val->intval);
	default:
		dev_err(chip->dev, "invalid property: %d\n", psp);
		return -EINVAL;
	}
}

static int smb_set_property(struct power_supply *psy,
			     enum power_supply_property psp,
			     const union power_supply_propval *val)
{
	struct smb_chip *chip = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		return regmap_update_bits(chip->regmap, chip->base + USBIN_CMD_IL,
					  USBIN_SUSPEND_BIT, !val->intval);
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		if (chip->gen != SMB5)
			return smb_set_current_limit(chip, val->intval);
		if (val->intval <= 0 || val->intval > 4800000)
			return -EINVAL;
		WRITE_ONCE(chip->user_icl_ua, val->intval);
		mod_delayed_work(system_wq, &chip->thermal_work, 0);
		return 0;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		if (chip->gen != SMB5 || val->intval <= 0 ||
		    val->intval >
			chip->batt_info->constant_charge_current_max_ua)
			return -EINVAL;
		WRITE_ONCE(chip->user_fcc_ua, val->intval);
		mod_delayed_work(system_wq, &chip->thermal_work, 0);
		return 0;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		if (chip->gen != SMB5 || val->intval < 0 ||
		    val->intval >= chip->num_thermal_levels)
			return -EINVAL;
		WRITE_ONCE(chip->user_cooling_state, val->intval);
		mod_delayed_work(system_wq, &chip->thermal_work, 0);
		return 0;
	default:
		dev_err(chip->dev, "No setter for property: %d\n", psp);
		return -EINVAL;
	}
}

static int smb_property_is_writable(struct power_supply *psy,
				     enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
	case POWER_SUPPLY_PROP_CURRENT_MAX:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		return 1;
	default:
		return 0;
	}
}

static irqreturn_t smb_handle_batt_overvoltage(int irq, void *data)
{
	struct smb_chip *chip = data;
	int rc;

	if (smbx_ov_status(chip) == 1) {
		dev_err(chip->dev, "battery overvoltage detected\n");
		WRITE_ONCE(chip->policy_health,
			   POWER_SUPPLY_HEALTH_OVERVOLTAGE);
		mutex_lock(&chip->policy_lock);
		rc = smb_set_charging_enabled(chip, false);
		if (!rc)
			chip->charging_enabled = false;
		mutex_unlock(&chip->policy_lock);
		if (rc)
			dev_err_ratelimited(chip->dev,
					    "failed to disable charging: %d\n", rc);
		if (chip->gen == SMB5)
			mod_delayed_work(system_wq, &chip->thermal_work, 0);
		power_supply_changed(chip->chg_psy);
	}

	return IRQ_HANDLED;
}

static irqreturn_t smb_handle_usb_plugin(int irq, void *data)
{
	struct smb_chip *chip = data;

	power_supply_changed(chip->chg_psy);

	schedule_delayed_work(&chip->status_change_work,
			      msecs_to_jiffies(1500));
	if (chip->gen == SMB5)
		mod_delayed_work(system_wq, &chip->thermal_work, 0);

	return IRQ_HANDLED;
}

static irqreturn_t smb_handle_usb_icl_change(int irq, void *data)
{
	struct smb_chip *chip = data;

	power_supply_changed(chip->chg_psy);

	return IRQ_HANDLED;
}

static irqreturn_t smb_handle_wdog_bark(int irq, void *data)
{
	struct smb_chip *chip = data;
	int rc;

	power_supply_changed(chip->chg_psy);

	rc = regmap_write(chip->regmap, BARK_BITE_WDOG_PET,
			  BARK_BITE_WDOG_PET_BIT);
	if (rc < 0)
		dev_err(chip->dev, "Couldn't pet the dog rc=%d\n", rc);

	return IRQ_HANDLED;
}

static const struct power_supply_desc smb_psy_desc = {
	.name = "SMB2_charger",
	.type = POWER_SUPPLY_TYPE_USB,
	.usb_types = BIT(POWER_SUPPLY_USB_TYPE_SDP) |
		     BIT(POWER_SUPPLY_USB_TYPE_CDP) |
		     BIT(POWER_SUPPLY_USB_TYPE_DCP) |
		     BIT(POWER_SUPPLY_USB_TYPE_UNKNOWN),
	.properties = smb_properties,
	.num_properties = ARRAY_SIZE(smb_properties),
	.get_property = smb_get_property,
	.set_property = smb_set_property,
	.property_is_writeable = smb_property_is_writable,
};

/* Init sequence derived from vendor downstream driver */
static const struct smb_init_register smb5_init_seq[] = {
	/* Override bootloader state before changing any other charger setting. */
	{ .addr = CHARGING_ENABLE_CMD,
	  .mask = CHARGING_ENABLE_CMD_BIT,
	  .val = 0 },
	{ .addr = USBIN_CMD_IL, .mask = USBIN_SUSPEND_BIT, .val = 0 },
	/*
	 * By default configure us as an upstream facing port
	 * FIXME: This will be handled by the type-c driver
	 */
	{ .addr = SMB5_TYPE_C_MODE_CFG,
	  .mask = SMB5_EN_TRY_SNK_BIT | SMB5_EN_SNK_ONLY_BIT,
	  .val = SMB5_EN_TRY_SNK_BIT },
	{ .addr = SMB5_TYPEC_TYPE_C_VCONN_CONTROL,
	  .mask = SMB5_VCONN_EN_ORIENTATION_BIT | SMB5_VCONN_EN_SRC_BIT |
		  SMB5_VCONN_EN_VALUE_BIT,
	  .val = SMB5_VCONN_EN_SRC_BIT },
	{ .addr = SMB5_DEBUG_ACCESS_SRC_CFG,
	  .mask = SMB5_EN_UNORIENTED_DEBUG_ACCESS_SRC_BIT,
	  .val = SMB5_EN_UNORIENTED_DEBUG_ACCESS_SRC_BIT },
	{ .addr = SMB5_TYPE_C_EXIT_STATE_CFG,
	  .mask = SMB5_SEL_SRC_UPPER_REF_BIT,
	  .val = SMB5_SEL_SRC_UPPER_REF_BIT },
	/*
	 * Disable Type-C factory mode and stay in Attached.SRC state when VCONN
	 * over-current happens
	 */
	{ .addr = TYPE_C_CFG,
	  .mask = APSD_START_ON_CC_BIT,
	  .val = 0 },
	{ .addr = SMB5_TYPE_C_DEBUG_ACCESS_SINK,
	  .mask = SMB5_TYPEC_DEBUG_ACCESS_SINK_MASK,
	  .val = 0x17 },
	/* Configure VBUS for software control */
	{ .addr = OTG_CFG, .mask = OTG_EN_SRC_CFG_BIT, .val = 0 },
	/*
	 * Recharge when State Of Charge drops below 98%.
	 */
	{ .addr = SMB5_CHARGE_RCHG_SOC_THRESHOLD_CFG_REG,
	  .mask = SMB5_CHARGE_RCHG_SOC_THRESHOLD_CFG_MASK,
	  .val = 250 },
	/* Enable BC1P2 auto Src detect */
	{ .addr = USBIN_OPTIONS_1_CFG,
	  .mask = AUTO_SRC_DETECT_BIT,
	  .val = AUTO_SRC_DETECT_BIT },
	/* Set the default SDP charger type to a 500ma USB 2.0 port */
	{ .addr = USBIN_ICL_OPTIONS,
	  .mask = USBIN_MODE_CHG_BIT,
	  .val = USBIN_MODE_CHG_BIT },
	{ .addr = CMD_ICL_OVERRIDE,
	  .mask = ICL_OVERRIDE_BIT,
	  .val = 0 },
	{ .addr = USBIN_LOAD_CFG,
	  .mask = ICL_OVERRIDE_AFTER_APSD_BIT,
	  .val = 0 },
	/* Disable watchdog */
	{ .addr = SNARL_BARK_BITE_WD_CFG, .mask = 0xff, .val = 0 },
	{ .addr = WD_CFG,
	  .mask = WATCHDOG_TRIGGER_AFP_EN_BIT | WDOG_TIMER_EN_ON_PLUGIN_BIT |
		  BARK_WDOG_INT_EN_BIT,
	  .val = 0 },
	/*
	 * Enable Automatic Input Current Limit, this will slowly ramp up the current
	 * When connected to a wall charger, and automatically stop when it detects
	 * the charger current limit (voltage drop?) or it reaches the programmed limit.
	 */
	{ .addr = USBIN_AICL_OPTIONS_CFG,
	  .mask = USBIN_AICL_PERIODIC_RERUN_EN_BIT | USBIN_AICL_ADC_EN_BIT
			| USBIN_AICL_EN_BIT | SUSPEND_ON_COLLAPSE_USBIN_BIT,
	  .val = USBIN_AICL_PERIODIC_RERUN_EN_BIT | USBIN_AICL_ADC_EN_BIT
			| USBIN_AICL_EN_BIT | SUSPEND_ON_COLLAPSE_USBIN_BIT },
};

/* Init sequence derived from vendor downstream driver */
static const struct smb_init_register smb2_init_seq[] = {
	/*
	 * By default configure us as an upstream facing port
	 * FIXME: This will be handled by the type-c driver
	 */
	{ .addr = TYPE_C_INTRPT_ENB_SOFTWARE_CTRL,
	  .mask = TYPEC_POWER_ROLE_CMD_MASK | SMB2_VCONN_EN_SRC_BIT |
		  VCONN_EN_VALUE_BIT,
	  .val = SMB2_VCONN_EN_SRC_BIT },
	/*
	 * Disable Type-C factory mode and stay in Attached.SRC state when VCONN
	 * over-current happens
	 */
	{ .addr = TYPE_C_CFG,
	  .mask = FACTORY_MODE_DETECTION_EN_BIT | VCONN_OC_CFG_BIT,
	  .val = 0 },
	/* Configure VBUS for software control */
	{ .addr = OTG_CFG, .mask = OTG_EN_SRC_CFG_BIT, .val = 0 },
	/*
	 * Use VBAT to determine the recharge threshold when battery is full
	 * rather than the state of charge.
	 */
	{ .addr = SMB2_FG_UPDATE_CFG_2_SEL,
	  .mask = SMB2_SOC_LT_CHG_RECHARGE_THRESH_SEL_BIT |
		  SMB2_VBT_LT_CHG_RECHARGE_THRESH_SEL_BIT,
	  .val = SMB2_VBT_LT_CHG_RECHARGE_THRESH_SEL_BIT },
	/* Enable charging */
	{ .addr = USBIN_OPTIONS_1_CFG, .mask = HVDCP_EN_BIT, .val = 0 },
	{ .addr = CHARGING_ENABLE_CMD,
	  .mask = CHARGING_ENABLE_CMD_BIT,
	  .val = CHARGING_ENABLE_CMD_BIT },
	/*
	 * Match downstream defaults
	 * CHG_EN_SRC_BIT - charger enable is controlled by software
	 * CHG_EN_POLARITY_BIT - polarity of charge enable pin when in HW control
	 *                       pulled low on OnePlus 6 and SHIFT6mq
	 * PRETOFAST_TRANSITION_CFG_BIT -
	 * BAT_OV_ECC_BIT -
	 * I_TERM_BIT - Current termination ?? 0 = enabled
	 * AUTO_RECHG_BIT - Enable automatic recharge when battery is full
	 *                  0 = enabled
	 * EN_ANALOG_DROP_IN_VBATT_BIT
	 * CHARGER_INHIBIT_BIT - Inhibit charging based on battery voltage
	 *                       instead of ??
	 */
	{ .addr = CHGR_CFG2,
	  .mask = CHG_EN_SRC_BIT | CHG_EN_POLARITY_BIT |
		  PRETOFAST_TRANSITION_CFG_BIT | BAT_OV_ECC_BIT | I_TERM_BIT |
		  AUTO_RECHG_BIT | EN_ANALOG_DROP_IN_VBATT_BIT |
		  CHARGER_INHIBIT_BIT,
	  .val = CHARGER_INHIBIT_BIT },
	/* STAT pin software override, match downstream. Parallel charging? */
	{ .addr = STAT_CFG,
	  .mask = STAT_SW_OVERRIDE_CFG_BIT,
	  .val = STAT_SW_OVERRIDE_CFG_BIT },
	/* Set the default SDP charger type to a 500ma USB 2.0 port */
	{ .addr = USBIN_ICL_OPTIONS,
	  .mask = USB51_MODE_BIT | USBIN_MODE_CHG_BIT,
	  .val = USB51_MODE_BIT },
	/* Disable watchdog */
	{ .addr = SNARL_BARK_BITE_WD_CFG, .mask = 0xff, .val = 0 },
	{ .addr = WD_CFG,
	  .mask = WATCHDOG_TRIGGER_AFP_EN_BIT | WDOG_TIMER_EN_ON_PLUGIN_BIT |
		  BARK_WDOG_INT_EN_BIT,
	  .val = 0 },
	/* These bits aren't documented anywhere */
	{ .addr = USBIN_5V_AICL_THRESHOLD_CFG,
	  .mask = USBIN_5V_AICL_THRESHOLD_CFG_MASK,
	  .val = 0x3 },
	{ .addr = USBIN_CONT_AICL_THRESHOLD_CFG,
	  .mask = USBIN_CONT_AICL_THRESHOLD_CFG_MASK,
	  .val = 0x3 },
	/*
	 * Enable Automatic Input Current Limit, this will slowly ramp up the current
	 * When connected to a wall charger, and automatically stop when it detects
	 * the charger current limit (voltage drop?) or it reaches the programmed limit.
	 */
	{ .addr = USBIN_AICL_OPTIONS_CFG,
	  .mask = USBIN_AICL_START_AT_MAX_BIT | USBIN_AICL_ADC_EN_BIT |
		  USBIN_AICL_EN_BIT | SUSPEND_ON_COLLAPSE_USBIN_BIT |
		  USBIN_HV_COLLAPSE_RESPONSE_BIT |
		  USBIN_LV_COLLAPSE_RESPONSE_BIT,
	  .val = USBIN_HV_COLLAPSE_RESPONSE_BIT |
		 USBIN_LV_COLLAPSE_RESPONSE_BIT | USBIN_AICL_EN_BIT },
	/*
	 * Set pre charge current to default, the OnePlus 6 bootloader
	 * sets this very conservatively.
	 */
	{ .addr = PRE_CHARGE_CURRENT_CFG,
	  .mask = PRE_CHARGE_CURRENT_SETTING_MASK,
	  .val = 500000 / CURRENT_SCALE_FACTOR },
};

struct smb_match_data pmi8998_match_data = {
	.init_seq = smb2_init_seq,
	.init_seq_len = ARRAY_SIZE(smb2_init_seq),
	.name = "pmi8998",
	.gen = SMB2,
	.current_step_ua = 25000,
	.fv_min_uv = 3487500,
	.fv_max_uv = 4920000,
	.fv_step_uv = 7500,
};

struct smb_match_data pm660_match_data = {
	.init_seq = smb2_init_seq,
	.init_seq_len = ARRAY_SIZE(smb2_init_seq),
	.name = "pm660",
	.gen = SMB2,
	.current_step_ua = 25000,
	.fv_min_uv = 3487500,
	.fv_max_uv = 4920000,
	.fv_step_uv = 7500,
};

struct smb_match_data pm8150b_match_data = {
	.init_seq = smb5_init_seq,
	.init_seq_len = ARRAY_SIZE(smb5_init_seq),
	.name = "pm8150b",
	.gen = SMB5,
	.current_step_ua = 50000,
	.fv_min_uv = 3600000,
	.fv_max_uv = 4790000,
	.fv_step_uv = 10000,
};

struct smb_match_data pm7250b_match_data = {
	.init_seq = smb5_init_seq,
	.init_seq_len = ARRAY_SIZE(smb5_init_seq),
	.name = "pm7250b",
	.gen = SMB5,
	.current_step_ua = 50000,
	.fv_min_uv = 3600000,
	.fv_max_uv = 4790000,
	.fv_step_uv = 10000,
};


static int smb_init_hw(struct smb_chip *chip, const struct smb_init_register *init_seq, size_t len)
{
	int rc, i;

	for (i = 0; i < len; i++) {
		dev_dbg(chip->dev, "%d: Writing 0x%02x to 0x%02x\n", i,
			init_seq[i].val, init_seq[i].addr);
		rc = regmap_update_bits(chip->regmap,
					chip->base + init_seq[i].addr,
					init_seq[i].mask,
					init_seq[i].val);
		if (rc < 0)
			return dev_err_probe(chip->dev, rc,
					     "%s: init command %d failed\n",
					     __func__, i);
	}

	return 0;
}

static int smb_init_irq(struct smb_chip *chip, int *irq, const char *name,
			 irqreturn_t (*handler)(int irq, void *data))
{
	int irqnum;
	int rc;

	irqnum = platform_get_irq_byname(to_platform_device(chip->dev), name);
	if (irqnum < 0)
		return irqnum;

	rc = devm_request_threaded_irq(chip->dev, irqnum, NULL, handler,
				       IRQF_ONESHOT, name, chip);
	if (rc < 0)
		return dev_err_probe(chip->dev, rc, "Couldn't request irq %s\n",
				     name);

	if (irq)
		*irq = irqnum;

	return 0;
}

static int smb_read_limit_table(struct smb_chip *chip, const char *property,
				u32 **table, unsigned int *count)
{
	int entries, rc;
	unsigned int i;

	entries = device_property_count_u32(chip->dev, property);
	if (entries < 0)
		return entries;
	if (!entries || entries > 32)
		return -EINVAL;

	*table = devm_kcalloc(chip->dev, entries, sizeof(**table), GFP_KERNEL);
	if (!*table)
		return -ENOMEM;

	rc = device_property_read_u32_array(chip->dev, property, *table,
					    entries);
	if (rc < 0)
		return rc;

	for (i = 0; i < entries; i++) {
		if (!(*table)[i] ||
		    (i && (*table)[i] > (*table)[i - 1]))
			return dev_err_probe(chip->dev, -EINVAL,
				"%s must contain non-zero descending limits\n",
				property);
	}

	*count = entries;
	return 0;
}

static int smb_parse_thermal_tables(struct smb_chip *chip)
{
	unsigned int input_count;
	int rc;

	if (!device_property_present(chip->dev,
				     "qcom,thermal-mitigation-microamp") &&
	    !device_property_present(chip->dev,
				     "qcom,thermal-mitigation-input-microamp")) {
		chip->thermal_levels = devm_kmalloc(
			chip->dev, sizeof(*chip->thermal_levels), GFP_KERNEL);
		chip->thermal_input_levels = devm_kmalloc(
			chip->dev, sizeof(*chip->thermal_input_levels), GFP_KERNEL);
		if (!chip->thermal_levels || !chip->thermal_input_levels)
			return -ENOMEM;
		chip->thermal_levels[0] = SMB5_FAST_CHARGE_CURRENT_UA;
		chip->thermal_input_levels[0] = 4800000;
		chip->num_thermal_levels = 1;
		return 0;
	}

	if (!device_property_present(chip->dev,
				     "qcom,thermal-mitigation-microamp") ||
	    !device_property_present(chip->dev,
				     "qcom,thermal-mitigation-input-microamp"))
		return dev_err_probe(chip->dev, -EINVAL,
			"both thermal FCC and ICL tables are required\n");

	rc = smb_read_limit_table(chip, "qcom,thermal-mitigation-microamp",
				  &chip->thermal_levels,
				  &chip->num_thermal_levels);
	if (rc < 0)
		return rc;

	rc = smb_read_limit_table(
		chip, "qcom,thermal-mitigation-input-microamp",
		&chip->thermal_input_levels, &input_count);
	if (rc < 0)
		return rc;
	if (input_count != chip->num_thermal_levels)
		return dev_err_probe(chip->dev, -EINVAL,
			"thermal FCC and ICL tables must have equal lengths\n");

	if (device_property_present(chip->dev,
				    "qcom,thermal-mitigation-dcp-microamp")) {
		rc = smb_read_limit_table(
			chip, "qcom,thermal-mitigation-dcp-microamp",
			&chip->thermal_dcp_levels, &input_count);
		if (rc < 0)
			return rc;
		if (input_count != chip->num_thermal_levels)
			return dev_err_probe(
				chip->dev, -EINVAL,
				"DCP thermal table must match FCC table length\n");
	}

	if (device_property_present(chip->dev,
				    "qcom,thermal-mitigation-pd-microamp")) {
		rc = smb_read_limit_table(
			chip, "qcom,thermal-mitigation-pd-microamp",
			&chip->thermal_pd_levels, &input_count);
		if (rc < 0)
			return rc;
		if (input_count != chip->num_thermal_levels)
			return dev_err_probe(
				chip->dev, -EINVAL,
				"PD thermal table must match FCC table length\n");
	}

	return 0;
}

static int smb_parse_jeita_ranges(struct smb_chip *chip)
{
	struct smb_jeita_range *range;
	u32 *values;
	int count, rc;
	unsigned int i;

	if (!device_property_present(chip->dev, "qcom,jeita-ranges"))
		return 0;

	count = device_property_count_u32(chip->dev, "qcom,jeita-ranges");
	if (count < 0)
		return count;
	if (!count || count % 4 || count > 32)
		return dev_err_probe(chip->dev, -EINVAL,
				     "qcom,jeita-ranges must contain 1..8 tuples\n");

	values = devm_kcalloc(chip->dev, count, sizeof(*values), GFP_KERNEL);
	chip->jeita_ranges = devm_kcalloc(chip->dev, count / 4,
					  sizeof(*chip->jeita_ranges), GFP_KERNEL);
	if (!values || !chip->jeita_ranges)
		return -ENOMEM;

	rc = device_property_read_u32_array(chip->dev, "qcom,jeita-ranges",
					    values, count);
	if (rc < 0)
		return rc;

	chip->num_jeita_ranges = count / 4;
	for (i = 0; i < chip->num_jeita_ranges; i++) {
		range = &chip->jeita_ranges[i];
		range->temp_min_decic = (s32)values[i * 4];
		range->temp_max_decic = (s32)values[i * 4 + 1];
		range->current_ua = values[i * 4 + 2];
		range->voltage_uv = values[i * 4 + 3];

		if (range->temp_min_decic > range->temp_max_decic ||
		    !range->current_ua || range->voltage_uv < chip->fv_min_uv ||
		    range->voltage_uv > chip->fv_max_uv ||
		    range->voltage_uv >
			chip->batt_info->voltage_max_design_uv ||
		    (i && range->temp_min_decic <=
			  chip->jeita_ranges[i - 1].temp_max_decic))
			return dev_err_probe(chip->dev, -EINVAL,
					     "invalid JEITA range %u\n", i);
	}

	return 0;
}

static int smb_cooling_get_max_state(struct thermal_cooling_device *cdev,
				     unsigned long *state)
{
	struct smb_chip *chip = cdev->devdata;

	*state = chip->num_thermal_levels - 1;
	return 0;
}

static int smb_cooling_get_cur_state(struct thermal_cooling_device *cdev,
				     unsigned long *state)
{
	struct smb_chip *chip = cdev->devdata;

	*state = READ_ONCE(chip->thermal_cooling_state);
	return 0;
}

static int smb_cooling_set_cur_state(struct thermal_cooling_device *cdev,
				     unsigned long state)
{
	struct smb_chip *chip = cdev->devdata;

	if (state >= chip->num_thermal_levels)
		return -EINVAL;
	if (state == READ_ONCE(chip->thermal_cooling_state))
		return 0;

	WRITE_ONCE(chip->thermal_cooling_state, state);
	mod_delayed_work(system_wq, &chip->thermal_work, 0);
	return 0;
}

static const struct thermal_cooling_device_ops smb_cooling_ops = {
	.get_max_state = smb_cooling_get_max_state,
	.get_cur_state = smb_cooling_get_cur_state,
	.set_cur_state = smb_cooling_set_cur_state,
};

static int smb_power_supply_notifier(struct notifier_block *nb,
				     unsigned long event, void *data)
{
	struct smb_chip *chip = container_of(nb, struct smb_chip, nb);
	struct power_supply *psy = data;

	if (event != PSY_EVENT_PROP_CHANGED)
		return NOTIFY_OK;
	if (psy != chip->input_psy &&
	    strcmp(psy->desc->name, "qcom-battery"))
		return NOTIFY_OK;

	mod_delayed_work(system_wq, &chip->thermal_work, 0);
	return NOTIFY_OK;
}

static void smb_unregister_power_supply_notifier(void *data)
{
	struct smb_chip *chip = data;

	power_supply_unreg_notifier(&chip->nb);
}

static int smb_probe(struct platform_device *pdev)
{
	struct power_supply_config supply_config = {};
	struct power_supply_desc *desc;
	struct smb_chip *chip;
	const struct smb_match_data *match_data;
	int rc, irq;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &pdev->dev;
	chip->name = pdev->name;

	chip->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!chip->regmap)
		return dev_err_probe(chip->dev, -ENODEV,
				     "failed to locate the regmap\n");

	rc = device_property_read_u32(chip->dev, "reg", &chip->base);
	if (rc < 0)
		return dev_err_probe(chip->dev, rc,
				     "Couldn't read base address\n");

	match_data = device_get_match_data(chip->dev);
	if (!match_data)
		return dev_err_probe(chip->dev, -EINVAL,
				     "missing charger match data\n");
	if (!match_data->current_step_ua || !match_data->fv_step_uv ||
	    match_data->fv_min_uv > match_data->fv_max_uv)
		return dev_err_probe(chip->dev, -EINVAL,
				     "invalid charger register parameters\n");

	chip->gen = match_data->gen;
	chip->current_step_ua = match_data->current_step_ua;
	chip->fv_min_uv = match_data->fv_min_uv;
	chip->fv_max_uv = match_data->fv_max_uv;
	chip->fv_step_uv = match_data->fv_step_uv;

	dev_info(chip->dev, "Generation %s\n",
		 chip->gen == SMB2 ? "SMB2" : "SMB5");

	/* Disable SMB5 before any probe deferral can preserve bootloader state. */
	if (chip->gen == SMB5) {
		rc = smb_set_charging_enabled(chip, false);
		if (rc < 0)
			return dev_err_probe(chip->dev, rc,
					     "failed to enter fail-safe state\n");
	}

	chip->usb_in_v_chan = devm_iio_channel_get(chip->dev, "usbin_v");
	if (IS_ERR(chip->usb_in_v_chan))
		return dev_err_probe(chip->dev, PTR_ERR(chip->usb_in_v_chan),
				     "Couldn't get usbin_v IIO channel\n");

	chip->usb_in_i_chan = devm_iio_channel_get(chip->dev, "usbin_i");
	if (IS_ERR(chip->usb_in_i_chan)) {
		return dev_err_probe(chip->dev, PTR_ERR(chip->usb_in_i_chan),
				     "Couldn't get usbin_i IIO channel\n");
	}

	if (device_property_match_string(chip->dev, "io-channel-names",
					 "connector_temp") >= 0) {
		chip->connector_temp_chan = devm_iio_channel_get(
			chip->dev, "connector_temp");
		if (IS_ERR(chip->connector_temp_chan))
			return dev_err_probe(
				chip->dev, PTR_ERR(chip->connector_temp_chan),
				"Couldn't get connector temperature channel\n");
	}

	rc = smb_init_hw(chip, match_data->init_seq, match_data->init_seq_len);
	if (rc < 0)
		return rc;

	supply_config.drv_data = chip;
	supply_config.fwnode = dev_fwnode(&pdev->dev);

	desc = devm_kzalloc(chip->dev, sizeof(smb_psy_desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;
	memcpy(desc, &smb_psy_desc, sizeof(smb_psy_desc));
	desc->name =
		devm_kasprintf(chip->dev, GFP_KERNEL, "%s-charger",
			       match_data->name);
	if (!desc->name)
		return -ENOMEM;

	chip->chg_psy =
		devm_power_supply_register(chip->dev, desc, &supply_config);
	if (IS_ERR(chip->chg_psy))
		return dev_err_probe(chip->dev, PTR_ERR(chip->chg_psy),
				     "failed to register power supply\n");

	rc = power_supply_get_battery_info(chip->chg_psy, &chip->batt_info);
	if (rc)
		return dev_err_probe(chip->dev, rc,
				     "Failed to get battery info\n");
	if (chip->batt_info->constant_charge_current_max_ua == -EINVAL)
		chip->batt_info->constant_charge_current_max_ua = DCP_CURRENT_UA;
	if (chip->batt_info->constant_charge_voltage_max_uv == -EINVAL)
		chip->batt_info->constant_charge_voltage_max_uv =
			chip->batt_info->voltage_max_design_uv;

	mutex_init(&chip->policy_lock);
	chip->jeita_index = -1;
	chip->policy_health = POWER_SUPPLY_HEALTH_UNKNOWN;
	chip->source_current_ua = SDP_CURRENT_UA;
	chip->source_voltage_uv = 5000000;
	chip->usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;

	rc = devm_delayed_work_autocancel(chip->dev, &chip->status_change_work,
					  smb_status_change_work);
	if (rc)
		return dev_err_probe(chip->dev, rc,
				     "Failed to init status change work\n");

	if (chip->gen == SMB5) {
		if (device_property_present(chip->dev, "input-power-supply")) {
			chip->input_psy = devm_power_supply_get_by_reference(
				chip->dev, "input-power-supply");
			if (IS_ERR(chip->input_psy))
				return dev_err_probe(
					chip->dev, PTR_ERR(chip->input_psy),
					"Failed to get input power supply\n");
			if (!chip->input_psy)
				return dev_err_probe(
					chip->dev, -EPROBE_DEFER,
					"Input power supply is not ready\n");
		}

		rc = smb_parse_thermal_tables(chip);
		if (rc < 0)
			return rc;
		rc = smb_parse_jeita_ranges(chip);
		if (rc < 0)
			return rc;

		rc = devm_delayed_work_autocancel(chip->dev,
						  &chip->thermal_work,
						  smb_thermal_work);
		if (rc)
			return dev_err_probe(chip->dev, rc,
					     "Failed to init thermal work\n");
	}

	rc = smb_set_float_voltage(
		chip, chip->batt_info->constant_charge_voltage_max_uv);
	if (rc < 0)
		return dev_err_probe(chip->dev, rc, "Couldn't set vbat max\n");

	rc = smb_init_irq(chip, &irq, "bat-ov", smb_handle_batt_overvoltage);
	if (rc < 0)
		return rc;

	rc = smb_init_irq(chip, &chip->cable_irq, "usb-plugin",
			   smb_handle_usb_plugin);
	if (rc < 0)
		return rc;

	rc = smb_init_irq(chip, &irq, "usbin-icl-change",
			   smb_handle_usb_icl_change);
	if (rc < 0)
		return rc;
	rc = smb_init_irq(chip, &irq, "wdog-bark", smb_handle_wdog_bark);
	if (rc < 0)
		return rc;

	devm_device_init_wakeup(chip->dev);

	rc = devm_pm_set_wake_irq(chip->dev, chip->cable_irq);
	if (rc < 0)
		return dev_err_probe(chip->dev, rc, "Couldn't set wake irq\n");

	platform_set_drvdata(pdev, chip);

	/*
	 * This overrides all of the other current limits.  SMB5 starts at the
	 * reduced fail-safe value; smb_thermal_work raises it only after the
	 * hardware JEITA state has been read successfully as GOOD.
	 */
	rc = smb_set_fast_charge_current(chip, chip->gen == SMB5 ?
					 SMB5_THERMAL_LIMIT_CURRENT_UA :
					 SMB5_FAST_CHARGE_CURRENT_UA);
	if (rc < 0)
		return dev_err_probe(chip->dev, rc,
				     "Couldn't write fast charge current cfg");

	rc = regmap_write_bits(chip->regmap, chip->base + AICL_RERUN_TIME_CFG,
			       AICL_RERUN_TIME_MASK, AIC_RERUN_TIME_3_SECS);
	if (rc < 0)
		return dev_err_probe(chip->dev, rc,
				     "Couldn't write fast AICL rerun time");

	if (chip->gen == SMB5) {
		chip->cdev = devm_thermal_of_cooling_device_register(
			chip->dev, 0, "smb5-charger", chip,
			&smb_cooling_ops);
		if (IS_ERR(chip->cdev)) {
			rc = PTR_ERR(chip->cdev);
			chip->cdev = NULL;
			if (rc != -ENODEV)
				return dev_err_probe(
					chip->dev, rc,
					"Failed to register cooling device\n");
			dev_warn(chip->dev,
				 "thermal framework unavailable; using JEITA only\n");
		}

		chip->nb.notifier_call = smb_power_supply_notifier;
		rc = power_supply_reg_notifier(&chip->nb);
		if (rc)
			return dev_err_probe(
				chip->dev, rc,
				"Failed to register power-supply notifier\n");
		rc = devm_add_action_or_reset(
			chip->dev, smb_unregister_power_supply_notifier, chip);
		if (rc)
			return rc;
	}

	/* Initialise charger state */
	schedule_delayed_work(&chip->status_change_work, 0);
	if (chip->gen == SMB5)
		mod_delayed_work(system_wq, &chip->thermal_work, 0);

	return 0;
}

static const struct of_device_id smb_match_id_table[] = {
	{ .compatible = "qcom,pmi8998-charger", .data = &pmi8998_match_data },
	{ .compatible = "qcom,pm660-charger", .data = &pm660_match_data },
	{ .compatible = "qcom,pm7250b-charger", .data = &pm7250b_match_data },
	{ .compatible = "qcom,pm8150b-charger", .data = &pm8150b_match_data },
	{ /* sentinal */ }
};
MODULE_DEVICE_TABLE(of, smb_match_id_table);

static struct platform_driver qcom_spmi_smb = {
	.probe = smb_probe,
	.driver = {
		.name = "qcom-smbx-charger",
		.of_match_table = smb_match_id_table,
		},
};

module_platform_driver(qcom_spmi_smb);

MODULE_AUTHOR("Casey Connolly <casey.connolly@linaro.org>");
MODULE_DESCRIPTION("Qualcomm SMB2 Charger Driver");
MODULE_LICENSE("GPL");
