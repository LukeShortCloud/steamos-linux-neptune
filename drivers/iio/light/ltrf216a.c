// SPDX-License-Identifier: GPL-2.0-only
/*
 * LTRF216A Ambient Light Sensor
 *
 * Copyright (C) 2021 Lite-On Technology Corp (Singapore)
 * Author: Shi Zhigang <Zhigang.Shi@liteon.com>
 *
 * IIO driver for LTRF216A (7-bit I2C slave address 0x53).
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/pm.h>
#include <linux/delay.h>

#define LTRF216A_DRV_NAME "ltrf216a"

#define LTRF216A_MAIN_CTRL			0x00
#define LTRF216A_ALS_MEAS_RATE		0x04
#define LTRF216A_ALS_GAIN			0x05
#define LTRF216A_PART_ID			0x06
#define LTRF216A_MAIN_STATUS		0x07
#define LTRF216A_CLEAR_DATA_0		0x0A
#define LTRF216A_CLEAR_DATA_1		0x0B
#define LTRF216A_CLEAR_DATA_2		0x0C
#define LTRF216A_ALS_DATA_0			0x0D
#define LTRF216A_ALS_DATA_1			0x0E
#define LTRF216A_ALS_DATA_2			0x0F
#define LTRF216A_INT_CFG			0x19
#define LTRF216A_INT_PST 			0x1A
#define LTRF216A_ALS_THRES_UP_0		0x21
#define LTRF216A_ALS_THRES_UP_1		0x22
#define LTRF216A_ALS_THRES_UP_2		0x23
#define LTRF216A_ALS_THRES_LOW_0	0x24
#define LTRF216A_ALS_THRES_LOW_1	0x25
#define LTRF216A_ALS_THRES_LOW_2	0x26

static const int int_time_mapping[] = { 400000, 200000, 100000 };

struct ltrf216a_data {
	struct i2c_client *client;
	u32			int_time;
	u8			int_time_fac;
	u8			als_gain_fac;
	struct mutex mutex;
};

// open air. need to update based on TP transmission rate.
#define WIN_FAC	1

//#define DRV_RETURN_RAWDATA
#ifdef DRV_RETURN_RAWDATA
#define LTRF216A_DATA_CHANNEL(_data, _addr) { \
	.type = IIO_INTENSITY, \
	.modified = 1, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME), \
	.channel2 = IIO_MOD_LIGHT_##_data, \
	.address = _addr, \
}

static const struct iio_chan_spec ltrf216a_channels[] = {
	LTRF216A_DATA_CHANNEL(CLEAR, LTRF216A_CLEAR_DATA_0),
	LTRF216A_DATA_CHANNEL(GREEN, LTRF216A_ALS_DATA_0),
};
#else
static const struct iio_chan_spec ltrf216a_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_PROCESSED) |
			BIT(IIO_CHAN_INFO_INT_TIME),
	}
};
#endif

static IIO_CONST_ATTR_INT_TIME_AVAIL("0.1 0.2 0.4");

static struct attribute *ltrf216a_attributes[] = {
	&iio_const_attr_integration_time_available.dev_attr.attr,
	NULL
};

static const struct attribute_group ltrf216a_attribute_group = {
	.attrs = ltrf216a_attributes,
};

static int ltrf216a_init(struct iio_dev *indio_dev)
{
	int ret;
	struct ltrf216a_data *data = iio_priv(indio_dev);

	ret = i2c_smbus_read_byte_data(data->client, LTRF216A_MAIN_CTRL);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading LTRF216A_MAIN_CTRL\n");
		return ret;
	}

	/* enable sensor */
	ret |= 0x02;
	ret = i2c_smbus_write_byte_data(data->client, LTRF216A_MAIN_CTRL, ret);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error writing LTRF216A_MAIN_CTRL\n");
		return ret;
	}

	return 0;
}

static int ltrf216a_disable(struct iio_dev *indio_dev)
{
	int ret;
	struct ltrf216a_data *data = iio_priv(indio_dev);

	ret = i2c_smbus_write_byte_data(data->client, LTRF216A_MAIN_CTRL, 0);
	if (ret < 0)
		dev_err(&data->client->dev, "Error writing LTRF216A_MAIN_CTRL\n");

	return ret;
}

static int ltrf216a_set_it_time(struct ltrf216a_data *data, int itime)
{
	int i, ret, index = -1;
	u8 reg;

	for (i = 0; i < ARRAY_SIZE(int_time_mapping); i++) {
		if (int_time_mapping[i] == itime) {
			index = i;
			break;
		}
	}
	/* Make sure integration time index is valid */
	if (index < 0)
		return -EINVAL;

	if (index == 0)
	{
		reg = 0x03;
		data->int_time_fac = 4;
	}
	else if (index == 1)
	{
		reg = 0x13;
		data->int_time_fac = 2;
	}
	else
	{
		reg = (index << 4) | 0x02;
		data->int_time_fac = 1;
	}

	ret = i2c_smbus_write_byte_data(data->client, LTRF216A_ALS_MEAS_RATE, reg);
	if (ret < 0)
		return ret;

	data->int_time = itime;

	return 0;
}

static int ltrf216a_get_it_time(struct ltrf216a_data *data, int *val, int *val2)
{
	*val = 0;
	*val2 = data->int_time;

	return IIO_VAL_INT_PLUS_MICRO;
}

static int ltrf216a_read_data(struct ltrf216a_data *data, u8 addr)
{
	int ret;
	int tries = 25;
	int val_0, val_1, val_2;

	while (tries--) {
		ret = i2c_smbus_read_byte_data(data->client, LTRF216A_MAIN_STATUS);
		if (ret < 0)
			return ret;
		if (ret & 0x08)
			break;
		msleep(20);
	}

	val_0 = i2c_smbus_read_byte_data(data->client, addr);
	val_1 = i2c_smbus_read_byte_data(data->client, addr + 1);
	val_2 = i2c_smbus_read_byte_data(data->client, addr + 2);
	ret = (val_2 << 16) + (val_1 << 8) + val_0;

	return ret;
}

static int ltrf216a_get_lux(struct ltrf216a_data* data)
{
	int greendata, cleardata;
	int lux;

	greendata = ltrf216a_read_data(data, LTRF216A_ALS_DATA_0);
	cleardata = ltrf216a_read_data(data, LTRF216A_CLEAR_DATA_0);

	if ((greendata < 0) || (cleardata < 0))
		lux = 0;
	else
		lux = greendata * 8 * WIN_FAC / data->als_gain_fac / data->int_time_fac / 10;

	return lux;
}

static int ltrf216a_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan, int *val,
			   int *val2, long mask)
{
	int ret;
	struct ltrf216a_data *data = iio_priv(indio_dev);

	mutex_lock(&data->mutex);

	switch (mask) {
#ifdef DRV_RETURN_RAWDATA
	case IIO_CHAN_INFO_RAW:
		ret = ltrf216a_read_data(data, chan->address);
		if (ret < 0)
			return ret;
		*val = ret;
		ret = IIO_VAL_INT;
		break;
#else
	case IIO_CHAN_INFO_PROCESSED:
		ret = ltrf216a_get_lux(data);
		*val = ret;
		ret = IIO_VAL_INT;
		break;
#endif
	case IIO_CHAN_INFO_INT_TIME:
		ret = ltrf216a_get_it_time(data, val, val2);
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&data->mutex);

	return ret;
}

static int ltrf216a_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int val,
			    int val2, long mask)
{
	struct ltrf216a_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		if (val != 0)
			return -EINVAL;
		mutex_lock(&data->mutex);
		ret = ltrf216a_set_it_time(data, val2);
		mutex_unlock(&data->mutex);
		return ret;
	default:
		return -EINVAL;
	}
}

static const struct iio_info ltrf216a_info = {
	.read_raw	= ltrf216a_read_raw,
	.write_raw	= ltrf216a_write_raw,
	.attrs		= &ltrf216a_attribute_group,
};

static int ltrf216a_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct ltrf216a_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;

	mutex_init(&data->mutex);

	//indio_dev->dev.parent = &client->dev;
	indio_dev->info = &ltrf216a_info;
	indio_dev->name = LTRF216A_DRV_NAME;
	indio_dev->channels = ltrf216a_channels;
	indio_dev->num_channels = ARRAY_SIZE(ltrf216a_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = ltrf216a_init(indio_dev);
	if (ret < 0) {
		dev_err(&client->dev, "ltrf216a chip init failed\n");
		return ret;
	}
	data->int_time = 100000;
	data->int_time_fac = 1;
	data->als_gain_fac = 3;

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(&client->dev, "failed to register iio dev\n");
		goto err_init;
	}

	return 0;
err_init:
	ltrf216a_disable(indio_dev);
	return ret;
}

static int ltrf216a_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	iio_device_unregister(indio_dev);
	ltrf216a_disable(indio_dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int ltrf216a_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));

	return ltrf216a_disable(indio_dev);
}

static int ltrf216a_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));

	return ltrf216a_init(indio_dev);
}

static SIMPLE_DEV_PM_OPS(ltrf216a_pm_ops, ltrf216a_suspend, ltrf216a_resume);
#define LTRF216A_PM_OPS (&ltrf216a_pm_ops)
#else
#define LTRF216A_PM_OPS NULL
#endif

static const struct i2c_device_id ltrf216a_id[] = {
	{ LTRF216A_DRV_NAME, 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, ltrf216a_id);

static struct i2c_driver ltrf216a_driver = {
	.driver = {
		.name = LTRF216A_DRV_NAME,
		.pm = LTRF216A_PM_OPS,
	},
	.probe		= ltrf216a_probe,
	.remove		= ltrf216a_remove,
	.id_table	= ltrf216a_id,
};

module_i2c_driver(ltrf216a_driver);

MODULE_AUTHOR("Shi Zhigang <Zhigang.Shi@liteon.com>");
MODULE_DESCRIPTION("LTRF216A ambient light sensor driver");
MODULE_LICENSE("GPL v2");
