/*
 * AK09911 3-axis compass driver
 * Copyright (c) 2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/i2c.h>
#include <linux/acpi.h>
#include <linux/iio/iio.h>
#include <linux/interrupt.h>
#include <linux/hrtimer.h>

#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/events.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#define AK09911_REGMAP_NAME "ak09911_regmap"

#define AK09911_REG_WIA1		0x00
#define AK09911_REG_WIA2		0x01
#define AK09911_WIA1_VALUE		0x48
#define AK09911_WIA2_VALUE		0x05

#define AK09911_REG_ST1			0x10
#define AK09911_REG_HXL			0x11
#define AK09911_REG_HXH			0x12
#define AK09911_REG_HYL			0x13
#define AK09911_REG_HYH			0x14
#define AK09911_REG_HZL			0x15
#define AK09911_REG_HZH			0x16
#define AK09911_REG_ST2			0x18

#define AK09911_REG_ASAX		0x60
#define AK09911_REG_ASAY		0x61
#define AK09911_REG_ASAZ		0x62

#define AK09911_REG_CNTL1		0x30
#define AK09911_REG_CNTL2		0x31
#define AK09911_REG_CNTL3		0x32

#define AK09911_MODE_SNG_MEASURE	0x01
#define AK09911_MODE_CONTINUOUS_1	0x02
#define AK09911_MODE_CONTINUOUS_2	0x04
#define AK09911_MODE_CONTINUOUS_3	0x06
#define AK09911_MODE_CONTINUOUS_4	0x08
#define AK09911_MODE_SELF_TEST		0x10
#define AK09911_MODE_FUSE_ACCESS	0x1F
#define AK09911_MODE_POWERDOWN		0x00
#define AK09911_RESET_DATA		0x01

#define AK09911_REG_CNTL1		0x30
#define AK09911_REG_CNTL2		0x31
#define AK09911_REG_CNTL3		0x32

#define AK09911_MAX_REGS		0x63

#define RAW_TO_GAUSS(asa)	((((asa) + 128) * 6000) / 256)

#define AK09911_CNTL2_CONTINUOUS_1_BIT	BIT(1)
#define AK09911_CNTL2_CONTINUOUS_2_BIT  BIT(2)
#define AK09911_CNTL2_CONTINUOUS_3_BIT  BIT(3)

#define AK09911_CNTL2_CONTINUOUS_MASK	(AK09911_CNTL2_CONTINUOUS_1_BIT | \
					AK09911_CNTL2_CONTINUOUS_2_BIT | \
					AK09911_CNTL2_CONTINUOUS_3_BIT)
#define AK09911_CNTL2_CONTINUOUS_SHIFT	1

#define AK09911_MAX_CONVERSION_TIMEOUT		500
#define AK09911_CONVERSION_DONE_POLL_TIME	10
#define AK09911_CNTL2_CONTINUOUS_DEFAULT	0
#define IF_USE_REGMAP_INTERFACE			0

struct ak09911_data {
	struct i2c_client	*client;
	struct mutex		lock;
	u8			reg_cntl2;
	struct regmap		*regmap;
	u8			asa[3];
	long			raw_to_gauss[3];

	s16 buffer[3];
	int64_t timestamp;
};

static const struct {
	int val;
	int val2;
} ak09911_samp_freq[] = {  {10, 0},
			   {20, 0},
			   {50, 0},
			   {100, 0} };

static const int ak09911_index_to_reg[] = {
	AK09911_REG_HXL, AK09911_REG_HYL, AK09911_REG_HZL,
};

static int ak09911_set_mode(struct i2c_client *client, u8 mode)
{
	int ret;

	switch (mode) {
	case AK09911_MODE_SNG_MEASURE:
	case AK09911_MODE_SELF_TEST:
	case AK09911_MODE_FUSE_ACCESS:
	case AK09911_MODE_POWERDOWN:
	case AK09911_MODE_CONTINUOUS_1:
	case AK09911_MODE_CONTINUOUS_2:
	case AK09911_MODE_CONTINUOUS_3:
	case AK09911_MODE_CONTINUOUS_4:
		ret = i2c_smbus_write_byte_data(client,
						AK09911_REG_CNTL2, mode);
		if (ret < 0) {
			dev_err(&client->dev, "set_mode error\n");
			return ret;
		}
		break;
	default:
		dev_err(&client->dev,
			"%s: Unknown mode(%d).", __func__, mode);
		return -EINVAL;
	}
	/* After mode change to powerdown wait atleast 100us */
	if(mode == AK09911_MODE_POWERDOWN)
		usleep_range(100, 500);

	return ret;
}

/* Get Sensitivity Adjustment value */
static int ak09911_get_asa(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct ak09911_data *data = iio_priv(indio_dev);
	int ret;

	ret = ak09911_set_mode(client, AK09911_MODE_FUSE_ACCESS);
	if (ret < 0)
		return ret;

	/* Get asa data and store in the device data. */
	ret = i2c_smbus_read_i2c_block_data(client, AK09911_REG_ASAX,
					    3, data->asa);
	if (ret < 0) {
		dev_err(&client->dev, "Not able to read asa data\n");
		return ret;
	}

	ret = ak09911_set_mode(client,  AK09911_MODE_POWERDOWN);
	if (ret < 0)
		return ret;

	data->raw_to_gauss[0] = RAW_TO_GAUSS(data->asa[0]);
	data->raw_to_gauss[1] = RAW_TO_GAUSS(data->asa[1]);
	data->raw_to_gauss[2] = RAW_TO_GAUSS(data->asa[2]);

	return 0;
}

static int ak09911_verify_chip_id(struct i2c_client *client)
{
	u8 wia_val[2];
	int ret;

	ret = i2c_smbus_read_i2c_block_data(client, AK09911_REG_WIA1,
					    2, wia_val);
	if (ret < 0) {
		dev_err(&client->dev, "Error reading WIA\n");
		return ret;
	}

	dev_dbg(&client->dev, "WIA %02x %02x\n", wia_val[0], wia_val[1]);

	if (wia_val[0] != AK09911_WIA1_VALUE ||
					wia_val[1] != AK09911_WIA2_VALUE) {
		dev_err(&client->dev, "Device ak09911 not found\n");
		return -ENODEV;
	}

	return 0;
}

static int wait_conversion_complete_polled(struct ak09911_data *data)
{
	struct i2c_client *client = data->client;
	u8 read_status;
	u32 timeout_ms = AK09911_MAX_CONVERSION_TIMEOUT;
	int ret;

	/* Wait for the conversion to complete. */
	while (timeout_ms) {
		msleep_interruptible(AK09911_CONVERSION_DONE_POLL_TIME);
		ret = i2c_smbus_read_byte_data(client, AK09911_REG_ST1);
		if (ret < 0) {
			dev_err(&client->dev, "Error in reading ST1\n");
			return ret;
		}
		read_status = ret & 0x01;
		if (read_status)
			break;
		timeout_ms -= AK09911_CONVERSION_DONE_POLL_TIME;
	}
	if (!timeout_ms) {
		dev_err(&client->dev, "Conversion timeout happened\n");
		return -EINVAL;
	}

	return read_status;
}

static int ak09911_read_axis(struct iio_dev *indio_dev, int index, int *val)
{
	struct ak09911_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;
	int ret;
	u16 meas_reg;
	s16 raw;

	mutex_lock(&data->lock);

	ret = ak09911_set_mode(client,  AK09911_MODE_CONTINUOUS_4);
	if (ret < 0)
		goto fn_exit;

	/* Read data */
	ret = i2c_smbus_read_word_data(client, ak09911_index_to_reg[index]);
	if (ret < 0) {
		dev_err(&client->dev, "Read axis data fails\n");
		goto fn_exit;
	}
	meas_reg = ret;

	/* datasheet recommends reading ST2 register after each
	 * data read operation, read ST2 when we reach index z */
	 if(index == 2) {
	ret = i2c_smbus_read_byte_data(client, AK09911_REG_ST2);
		if (ret < 0) {
			dev_err(&client->dev, "Read AK09911_REG_ST2 reg fails\n");
			goto fn_exit;
		}
	}
	mutex_unlock(&data->lock);

	/* Endian conversion of the measured values. */
	raw = (s16) (le16_to_cpu(meas_reg));

	/* Clamp to valid range. */
	raw = clamp_t(s16, raw, -8192, 8192);
	*val = raw;
	return IIO_VAL_INT;

fn_exit:
	mutex_unlock(&data->lock);

	return ret;
}

static int ak09911_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2,
			   long mask)
{
	int ret, i;
#if IF_USE_REGMAP_INTERFACE
	unsigned int reg;
#endif
	struct ak09911_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		return ak09911_read_axis(indio_dev, chan->address, val);
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = data->raw_to_gauss[chan->address];
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_SAMP_FREQ:
#if IF_USE_REGMAP_INTERFACE
		mutex_lock(&data->lock);
		ret = regmap_read(data->regmap, AK09911_REG_CNTL2, &reg);
		mutex_unlock(&data->lock);
		if (ret < 0)
			return ret;

		i = (reg & AK09911_CNTL2_CONTINUOUS_MASK)
			>> AK09911_CNTL2_CONTINUOUS_SHIFT;
#else
		ret = i2c_smbus_read_byte_data(data->client, AK09911_REG_CNTL2);
		if (ret < 0) {
			dev_err(&data->client->dev, "Error in reading CNTL2\n");
			return ret;
		}

		data->reg_cntl2 = ret;

		i = data->reg_cntl2 >> AK09911_CNTL2_CONTINUOUS_SHIFT;
		if (i == 0) {
			*val  = 0;
			*val2 = 0;
			return IIO_VAL_INT_PLUS_MICRO;
		}
		i = i - 1;
		if (i < 0 || i >= ARRAY_SIZE(ak09911_samp_freq))
			return -EINVAL;

#endif
		*val  = ak09911_samp_freq[i].val;
		*val2 = ak09911_samp_freq[i].val2;
		return IIO_VAL_INT_PLUS_MICRO;
	}

	return -EINVAL;
}

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("10 20 50 100");

static struct attribute *ak09911_attributes[] = {
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group ak09911_attrs_group = {
	.attrs = ak09911_attributes,
};

enum ak09911_axis {
	AXIS_X = 0,
	AXIS_Y,
	AXIS_Z,
};

#define AK09911_CHANNEL(axis, index)					\
	{								\
		.type = IIO_MAGN,					\
		.modified = 1,						\
		.channel2 = IIO_MOD_##axis,				\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
				BIT(IIO_CHAN_INFO_SCALE),  		\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SAMP_FREQ),\
		.address = index,					\
		.scan_index = (AXIS_##axis), \
		.scan_type = { \
			.sign = 's', \
			.realbits = 16, \
			.storagebits = 16, \
			.endianness = IIO_CPU, \
		}, \
	}

static const struct iio_chan_spec ak09911_channels[] = {
	AK09911_CHANNEL(X, 0),
	AK09911_CHANNEL(Y, 1),
	AK09911_CHANNEL(Z, 2),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static int ak09911_get_samp_freq_index(struct ak09911_data *data,
					int val, int val2)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ak09911_samp_freq); i++)
		if (ak09911_samp_freq[i].val == val &&
			ak09911_samp_freq[i].val2 == val2)
				return i;
	return -EINVAL;
}

static int ak09911_write_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan, int val,
				int val2, long mask)
{
	struct ak09911_data *data = iio_priv(indio_dev);
	int ret, i;

	if (iio_buffer_enabled(indio_dev))
		return -EBUSY;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		i = ak09911_get_samp_freq_index(data, val, val2);
		if (i < 0)
			return -EINVAL;

		data->reg_cntl2 &= ~AK09911_CNTL2_CONTINUOUS_MASK;
		data->reg_cntl2 |= (i + 1) << AK09911_CNTL2_CONTINUOUS_SHIFT;

		mutex_lock(&data->lock);
#if IF_USE_REGMAP_INTERFACE
		ret = regmap_update_bits(data->regmap, AK09911_REG_CNTL2,
					AK09911_CNTL2_CONTINUOUS_MASK,
				(i + 1) << AK09911_CNTL2_CONTINUOUS_SHIFT);
#else
		/* When user wants to change operation mode,
		* transit to Power-down mode first and then
		* transit to other modes*/
		ret = i2c_smbus_write_byte_data(data->client,
				AK09911_REG_CNTL2, AK09911_MODE_POWERDOWN);
		if (ret < 0) {
			dev_err(&data->client->dev, "Error in switching to powerdown\n");
			return ret;
		}
		/*After Power-down mode is set, at least 100 μ s (Twat)
		 *  is needed before setting another mode*/
				usleep_range(100, 500);
		ret = i2c_smbus_write_byte_data(data->client,
				AK09911_REG_CNTL2, data->reg_cntl2);
#endif
		mutex_unlock(&data->lock);
		return ret;
	default:
		return -EINVAL;
	}
}

static const struct iio_info ak09911_info = {
	.attrs		= &ak09911_attrs_group,
	.read_raw	= ak09911_read_raw,
	.write_raw	= ak09911_write_raw,
	.driver_module	= THIS_MODULE,
};

static const struct acpi_device_id ak_acpi_match[] = {
	{"AK009911", 0},
	{ },
};
MODULE_DEVICE_TABLE(acpi, ak_acpi_match);

static bool ak09911_is_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case AK09911_REG_CNTL1:
	case AK09911_REG_CNTL2:
	case AK09911_REG_CNTL3:
		return true;
	default:
		return false;
	}
}

static bool ak09911_is_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case AK09911_REG_HXL:
	case AK09911_REG_HXH:
	case AK09911_REG_HYL:
	case AK09911_REG_HYH:
	case AK09911_REG_HZL:
	case AK09911_REG_HZH:
	case AK09911_REG_ST2:
	case AK09911_REG_ST1:
		return true;
	default:
		return false;
	}
}

static bool ak09911_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case AK09911_REG_CNTL1:
	case AK09911_REG_CNTL2:
	case AK09911_REG_CNTL3:
		return false;
	default:
		return true;
	}
}

static struct reg_default ak09911_reg_defaults[] = {
	{ AK09911_REG_CNTL1,  0x00 },
	{ AK09911_REG_CNTL2,  0x00 },
	{ AK09911_REG_CNTL3,  0x00 },
};

static const struct regmap_config ak09911_regmap_config = {
	.name = AK09911_REGMAP_NAME,

	.reg_bits = 8,
	.val_bits = 8,

	/* .max_register = AK09911_REG_ID, */
	.cache_type = REGCACHE_FLAT,

	.writeable_reg = ak09911_is_writeable_reg,
	.readable_reg  = ak09911_is_readable_reg,
	.volatile_reg  = ak09911_is_volatile_reg,

	.reg_defaults  = ak09911_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(ak09911_reg_defaults),
	.max_register = AK09911_MAX_REGS,
};
static irqreturn_t ak09911_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ak09911_data *data = iio_priv(indio_dev);
	int ret, i=0 ,j=0 ;
	u16 meas_reg;
	s16 raw;

	ret = ak09911_set_mode(data->client,  AK09911_MODE_CONTINUOUS_4);
	if (ret < 0)
		goto err;

	ret = wait_conversion_complete_polled(data);
	if (ret < 0)
		goto err;

	mutex_lock(&data->lock);
	for_each_set_bit(i, indio_dev->active_scan_mask,
		indio_dev->masklength) {

	ret = i2c_smbus_read_word_data(data->client, ak09911_index_to_reg[i]);
	if (ret < 0)
		goto err;

	meas_reg = ret;
	/* Endian conversion of the measured values. */
	raw = (s16) (le16_to_cpu(meas_reg));

	/* Clamp to valid range. */
	raw = clamp_t(s16, raw, -8192, 8192);
	data->buffer[j++] = raw;
	}

	/* datasheet recommends reading ST2 register after each
	 * data read operation */
	ret = i2c_smbus_read_byte_data(data->client, AK09911_REG_ST2);
	if (ret < 0)
		goto err;

	iio_push_to_buffers_with_timestamp(indio_dev, data->buffer,
					   pf->timestamp);
err:
	mutex_unlock(&data->lock);
	iio_trigger_notify_done(indio_dev->trig);
	return IRQ_HANDLED;
}

static int ak09911_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct ak09911_data *data;
	struct regmap *regmap;
	const char *name;
	int ret;

	ret = ak09911_verify_chip_id(client);
	if (ret) {
		dev_err(&client->dev, "AK00911 not detected\n");
		return -ENOSYS;
	}

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (indio_dev == NULL)
		return -ENOMEM;

	regmap = devm_regmap_init_i2c(client, &ak09911_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "regmap initialization failed\n");
		return PTR_ERR(regmap);
	}

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);

	data->client = client;
	data->regmap = regmap;
	mutex_init(&data->lock);

	ret = ak09911_get_asa(client);
	if (ret)
		return ret;

	if (id)
		name = (char *) id->name;
	else if (ACPI_HANDLE(&client->dev))
		name = (char *)dev_name(&client->dev);
	else
		return -ENOSYS;

	dev_dbg(&client->dev, "Asahi compass chip %s\n", name);

	indio_dev->dev.parent = &client->dev;
	indio_dev->channels = ak09911_channels;
	indio_dev->num_channels = ARRAY_SIZE(ak09911_channels);
	indio_dev->info = &ak09911_info;
	indio_dev->name = id->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->name = name;

	data->reg_cntl2 = AK09911_CNTL2_CONTINUOUS_DEFAULT;


	ret = iio_triggered_buffer_setup(indio_dev,
					 &iio_pollfunc_store_time,
					 ak09911_trigger_handler,
					 /*&ak09911_buffer_setup_ops*/ NULL);
	if (ret < 0) {
		dev_err(&client->dev, "iio triggered buffer setup failed\n");
		return ret;
	}

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		return ret;

	return 0;
}

static int ak09911_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);

	return 0;
}

static const struct i2c_device_id ak09911_id[] = {
	{"ak09911", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, ak09911_id);

static struct i2c_driver ak09911_driver = {
	.driver = {
		.name	= "ak09911",
		.acpi_match_table = ACPI_PTR(ak_acpi_match),
	},
	.probe		= ak09911_probe,
	.remove		= ak09911_remove,
	.id_table	= ak09911_id,
};
module_i2c_driver(ak09911_driver);

MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("AK09911 Compass driver");
