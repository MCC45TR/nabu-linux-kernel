// SPDX-License-Identifier: GPL-2.0-only
/*
 * Standard IIO colour-temperature endpoint for Qualcomm SSC sensors whose
 * transport is owned by a userspace FastRPC/QMI service.
 */
#include <linux/err.h>
#include <linux/iio/iio.h>
#include <linux/iio/sw_device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#define SSC_CCT_MIN_KELVIN 1000
#define SSC_CCT_MAX_KELVIN 40000

/**
 * struct ssc_cct_state - latest userspace-supplied measurement
 * @lock: serializes readers and the single privileged writer
 * @kelvin: validated colour temperature
 * @valid: whether a first measurement has been received
 */
struct ssc_cct_state {
	struct mutex lock; /* protects kelvin and valid */
	int kelvin;
	bool valid;
};

static const struct config_item_type ssc_cct_config_type = {
	.ct_owner = THIS_MODULE,
};

static const struct iio_chan_spec ssc_cct_channels[] = {
	{
		.type = IIO_COLORTEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	},
};

static int ssc_cct_read_raw(struct iio_dev *indio_dev,
			    const struct iio_chan_spec *channel,
			    int *val, int *val2, long mask)
{
	struct ssc_cct_state *state = iio_priv(indio_dev);
	int ret = IIO_VAL_INT;

	if (mask != IIO_CHAN_INFO_RAW)
		return -EINVAL;

	mutex_lock(&state->lock);
	if (!state->valid)
		ret = -EAGAIN;
	else
		*val = state->kelvin;
	mutex_unlock(&state->lock);
	return ret;
}

static int ssc_cct_write_raw(struct iio_dev *indio_dev,
			     const struct iio_chan_spec *channel,
			     int val, int val2, long mask)
{
	struct ssc_cct_state *state = iio_priv(indio_dev);

	if (mask != IIO_CHAN_INFO_RAW || val2 || val < 0 ||
	    (val && (val < SSC_CCT_MIN_KELVIN || val > SSC_CCT_MAX_KELVIN)))
		return -EINVAL;

	mutex_lock(&state->lock);
	state->kelvin = val;
	state->valid = val != 0;
	mutex_unlock(&state->lock);
	return 0;
}

static const struct iio_info ssc_cct_info = {
	.read_raw = ssc_cct_read_raw,
	.write_raw = ssc_cct_write_raw,
};

static struct iio_sw_device *ssc_cct_probe(const char *name)
{
	struct iio_sw_device *software_device;
	struct ssc_cct_state *state;
	struct iio_dev *indio_dev;
	int ret;

	software_device = kzalloc_obj(*software_device);
	if (!software_device)
		return ERR_PTR(-ENOMEM);

	indio_dev = iio_device_alloc(NULL, sizeof(*state));
	if (!indio_dev) {
		ret = -ENOMEM;
		goto free_software_device;
	}

	indio_dev->name = kstrdup(name, GFP_KERNEL);
	if (!indio_dev->name) {
		ret = -ENOMEM;
		goto free_iio_device;
	}

	state = iio_priv(indio_dev);
	mutex_init(&state->lock);
	indio_dev->channels = ssc_cct_channels;
	indio_dev->num_channels = ARRAY_SIZE(ssc_cct_channels);
	indio_dev->info = &ssc_cct_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto free_name;

	software_device->device = indio_dev;
	iio_swd_group_init_type_name(software_device, name,
				     &ssc_cct_config_type);
	return software_device;

free_name:
	kfree(indio_dev->name);
free_iio_device:
	iio_device_free(indio_dev);
free_software_device:
	kfree(software_device);
	return ERR_PTR(ret);
}

static int ssc_cct_remove(struct iio_sw_device *software_device)
{
	struct iio_dev *indio_dev = software_device->device;

	iio_device_unregister(indio_dev);
	kfree(indio_dev->name);
	iio_device_free(indio_dev);
	kfree(software_device);
	return 0;
}

static const struct iio_sw_device_ops ssc_cct_ops = {
	.probe = ssc_cct_probe,
	.remove = ssc_cct_remove,
};

static struct iio_sw_device_type ssc_cct_device = {
	.name = "ssc_cct",
	.owner = THIS_MODULE,
	.ops = &ssc_cct_ops,
};

module_iio_sw_device_driver(ssc_cct_device);

MODULE_AUTHOR("mcc45tr <mcc45tr@gmail.com>");
MODULE_DESCRIPTION("Qualcomm SSC userspace colour temperature IIO endpoint");
MODULE_LICENSE("GPL");
