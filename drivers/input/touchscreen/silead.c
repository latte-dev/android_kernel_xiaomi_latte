/* -------------------------------------------------------------------------
 * Copyright (C) 2014-2015, Intel Corporation
 *
 * Derived from:
 *  gslX68X.c
 *  Copyright (C) 2010-2015, Shanghai Sileadinc Co.Ltd
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 * ------------------------------------------------------------------------- */
#define DEBUG

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/interrupt.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/pm.h>
#include <linux/of_gpio.h>
#ifdef CONFIG_PM
#include <linux/power_hal_sysfs.h>
#endif

#include "silead.h"
#define SILEAD_TS_NAME "silead_ts"
#define SILEAD_X_B       0x08
#define SILEAD_X_A          0x0a
#define SILEAD_REG_RESET	0xE0
#define SILEAD_REG_DATA		0x80
#define SILEAD_REG_TOUCH_NR	0x80
#define SILEAD_REG_POWER	0xBC
#define SILEAD_REG_CLOCK	0xE4
#define SILEAD_REG_STATUS	0xB0
#define SILEAD_REG_ID		0xFC
#define SILEAD_REG_MEM_CHECK	0xB0

#define SILEAD_STATUS_OK	0x5A5A5A5A
#define SILEAD_TS_DATA_LEN	44

#define SILEAD_CLOCK		0x04
#define SILEAD_CLOCK_OFF	0xB5

#define SILEAD_TOUCH_NR		0x03

#define SILEAD_CMD_RESET	0x88
#define SILEAD_CMD_START	0x00

#define SILEAD_POINT_DATA_LEN	0x04
#define SILEAD_POINT_Y_OFF      0x00
#define SILEAD_POINT_Y_MSB_OFF	0x01
#define SILEAD_POINT_X_OFF	0x02
#define SILEAD_POINT_X_MSB_OFF	0x03
#define SILEAD_POINT_ID_OFF	0x03
#define SILEAD_X_HSB_MASK	0xF0
#define SILEAD_POINT_HSB_MASK	0x0F
#define SILEAD_TOUCH_ID_MASK	0x0F

#define SILEAD_DT_FW_NAME	"fw-name"
#define SILEAD_DT_X_MAX		"resolution-x"
#define SILEAD_DT_Y_MAX		"resolution-y"
#define SILEAD_DT_MAX_FINGERS	"max-fingers"
#define SILEAD_DT_PRESSURE	"pressure"

#define SILEAD_IRQ_GPIO_NAME	"irq-gpio"
#define SILEAD_PWR_GPIO_NAME	"power-gpio"

#define SILEAD_FW_NAME		"silead.fw"
#define SILEAD_X_MAX		960
#define SILEAD_Y_MAX		600
#define SILEAD_MAX_FINGERS	5
#define SILEAD_PRESSURE		50
#define SILEAD_FW_NAME_LEN	30

enum silead_ts_power {
	SILEAD_POWER_ON  = 1,
	SILEAD_POWER_OFF = 0
};

#define GSL_ALG_ID
#ifdef GSL_ALG_ID


static unsigned int *gsl_config_data_id;
const struct firmware *fw;
#endif
struct silead_ts_data {
	struct i2c_client *client;
	struct gpio_desc *gpio_irq;
	struct gpio_desc *gpio_power;
	struct input_dev *input_dev;
	u16 x_max;
	u16 y_max;
	u8 max_fingers;
	u8 pressure;
	char fw_name[SILEAD_FW_NAME_LEN];
	u32 chip_id;
	u8 xy_swap;
	u8 x_invert;
	u8 y_invert;
	struct input_mt_pos pos[SILEAD_MAX_FINGERS];
	int slots[SILEAD_MAX_FINGERS];
};

struct silead_fw_data {
	u32 offset;
	u32 val;
};

static int silead_ts_request_input_dev(struct silead_ts_data *data)
{
	struct device *dev = &data->client->dev;
	int ret;

	data->input_dev = devm_input_allocate_device(dev);
	if (!data->input_dev) {
		dev_err(dev,
			"Failed to allocate input device\n");
		return -ENOMEM;
	}

	data->input_dev->evbit[0] = BIT_MASK(EV_SYN) |
				    BIT_MASK(EV_KEY) |
				    BIT_MASK(EV_ABS);
	input_set_abs_params(data->input_dev, ABS_MT_TRACKING_ID, 0, 10, 0, 0);
	input_set_abs_params(data->input_dev, ABS_MT_POSITION_X, 0,
			     800, 0, 0);
	input_set_abs_params(data->input_dev, ABS_MT_POSITION_Y, 0,
			     1280, 0, 0);
	input_set_abs_params(data->input_dev, ABS_MT_TOUCH_MAJOR, 0,
			     255, 0, 0);
	input_set_abs_params(data->input_dev, ABS_MT_WIDTH_MAJOR, 0,
			     200, 0, 0);

	input_mt_init_slots(data->input_dev, data->max_fingers,
			    INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED |
			    INPUT_MT_TRACK);

	data->input_dev->name = SILEAD_TS_NAME;
	data->input_dev->phys = "input/ts";
	data->input_dev->id.bustype = BUS_I2C;

	ret = input_register_device(data->input_dev);
	if (ret) {
		dev_err(dev, "Failed to register input device: %d\n", ret);
		return ret;
	}

	return 0;
}

static void silead_ts_report_touch(struct silead_ts_data *data,
		struct gsl_touch_info *cinfo, u8 i, bool swap)
{
	input_mt_slot(data->input_dev, cinfo->id[i]-1);
	input_mt_report_slot_state(data->input_dev, MT_TOOL_FINGER, true);
	input_report_abs(data->input_dev, ABS_MT_TRACKING_ID, cinfo->id[i]-1);
	input_report_abs(data->input_dev, ABS_MT_POSITION_X,
			(swap ? cinfo->y[i] : cinfo->x[i]));
	input_report_abs(data->input_dev, ABS_MT_POSITION_Y,
			(swap ? cinfo->x[i] : cinfo->y[i]));
	input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, data->pressure);
	input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 1);
}

static void silead_ts_set_power(struct i2c_client *client,
				enum silead_ts_power state)
{
	int ret;
	struct device *dev = &client->dev;
	struct silead_ts_data *data = i2c_get_clientdata(client);

	if (data->gpio_power)
		gpiod_set_value_cansleep(data->gpio_power, state);
	else {
		if (state == SILEAD_POWER_ON) {
			ret = i2c_smbus_write_byte_data(client, SILEAD_REG_CLOCK,
					SILEAD_CLOCK);
		}
		else {
			ret = i2c_smbus_write_byte_data(client, SILEAD_REG_CLOCK,
					SILEAD_CLOCK_OFF);
		}
		if (ret < 0)
			dev_err(dev, "Data read error %d\n", ret);
	}
}

static void silead_ts_read_data(struct i2c_client *client)
{
	struct silead_ts_data *data = i2c_get_clientdata(client);
	struct device *dev = &client->dev;
	u8 buf[SILEAD_TS_DATA_LEN];
	char read_buf[4];
	u8 buf_t[4] = {0};
	int x, y, id, touch_nr, ret, i, offset;
	struct gsl_touch_info cinfo = { { 0 } };
	ret = i2c_smbus_read_i2c_block_data(client, SILEAD_REG_DATA,
					    SILEAD_TS_DATA_LEN, buf);
	if (ret < 0) {
		dev_err(dev, "Data read error %d\n", ret);
		return;
	}

	cinfo.finger_num = buf[0];
	if (cinfo.finger_num < 0)
		return;

	for (i = 0; i < cinfo.finger_num && i < 10; i++) {
		cinfo.id[i] = buf[i*4+4+3] >> 4;
		cinfo.y[i] = (buf[i*4+4] | ((buf[i*4+5]) << 8));
		cinfo.x[i] = (buf[i*4+6] | ((buf[i*4+7] & 0x0f) << 8));
	}

	input_mt_assign_slots(data->input_dev, data->slots, data->pos, touch_nr);

#ifdef GSL_ALG_ID
	int tmp1 = 0;
	cinfo.finger_num = (buf[3]<<24)|(buf[2]<<16)|(buf[1]<<8)|(buf[0]);
	gsl_alg_id_main(&cinfo);

	tmp1 = gsl_mask_tiaoping();
	dev_dbg(dev, "tmp1: %d\n", tmp1);
	if (tmp1 > 0 && tmp1 < 0xffffffff) {
		i2c_smbus_write_byte_data(client, SILEAD_X_HSB_MASK,
					SILEAD_X_A);

		buf_t[0] = (u8)(tmp1 & 0xff);
		buf_t[1] = (u8)((tmp1 >> 8) & 0xff);
		buf_t[2] = (u8)((tmp1 >> 16) & 0xff);
		buf_t[3] = (u8)((tmp1 >> 24) & 0xff);
		dev_dbg(dev, "tmp1 = %08x, buf[0..3] = %02x : %02x : %02x : %02x\n",
				tmp1, buf_t[0], buf_t[1], buf_t[2], buf_t[3]);
		i2c_smbus_write_i2c_block_data(client,   SILEAD_X_B, 4, buf_t);
	}
#endif

	for (i = 0; i < cinfo.finger_num; i++) {
		/* the i8880 with the di_xian panel firmware is badly broken for
		 * multi touch We test for the firmware version and don't use
		 * the data slot information but instead use the touch number
		 */
		if (strstr(data->fw_name, "di_xian") != NULL) {
			dev_dbg(dev, "%s in use no data slot. using touch_nr\n",
					data->fw_name);
			id = cinfo.id;
		}

		silead_ts_report_touch(data, &cinfo, i, data->xy_swap);
		dev_dbg(dev, "x=%d y=%d sw_id=%d\n", cinfo.x[i], cinfo.y[i],
				cinfo.id[i]);
	}

	input_mt_sync_frame(data->input_dev);
	input_sync(data->input_dev);
}

static int silead_ts_init(struct i2c_client *client)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, SILEAD_REG_RESET,
					SILEAD_CMD_RESET);
	if (ret)
		goto i2c_write_err;
	usleep_range(10000, 15000);

	ret = i2c_smbus_write_byte_data(client, SILEAD_REG_TOUCH_NR,
					SILEAD_TOUCH_NR);
	if (ret)
		goto i2c_write_err;
	usleep_range(10000, 15000);

	ret = i2c_smbus_write_byte_data(client, SILEAD_REG_CLOCK, SILEAD_CLOCK);
	if (ret)
		goto i2c_write_err;
	usleep_range(10000, 15000);

	ret = i2c_smbus_write_byte_data(client, SILEAD_REG_RESET,
					SILEAD_CMD_START);
	if (ret)
		goto i2c_write_err;
	usleep_range(10000, 15000);

	return 0;

i2c_write_err:
	dev_err(&client->dev, "Registers clear error %d\n", ret);
	return ret;
}

static int silead_ts_reset(struct i2c_client *client)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, SILEAD_REG_RESET,
					SILEAD_CMD_RESET);
	if (ret)
		goto i2c_write_err;
	usleep_range(10000, 15000);

	ret = i2c_smbus_write_byte_data(client, SILEAD_REG_CLOCK, SILEAD_CLOCK);
	if (ret)
		goto i2c_write_err;
	usleep_range(10000, 15000);

	ret = i2c_smbus_write_byte_data(client, SILEAD_REG_POWER,
					SILEAD_CMD_START);
	if (ret)
		goto i2c_write_err;
	usleep_range(10000, 15000);

	return 0;

i2c_write_err:
	dev_err(&client->dev, "Chip reset error %d\n", ret);
	return ret;
}

static int silead_ts_startup(struct i2c_client *client)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, SILEAD_REG_RESET, 0x00);
	if (ret) {
		dev_err(&client->dev, "Startup error %d\n", ret);
		return ret;
	}

	usleep_range(10000, 15000);
	return 0;
}

static int silead_ts_load_fw(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct silead_ts_data *data = i2c_get_clientdata(client);
	unsigned int fw_size, i;
	struct silead_fw_data *fw_data;
	int ret;

	ret = request_firmware(&fw, data->fw_name, dev);
	if (ret) {
		dev_err(dev, "Firmware request error %d\n", ret);
		return ret;
	}

	gsl_config_data_id = fw->data;

	fw_size = (fw->size - 2048) / sizeof(*fw_data);
	fw_data = (struct silead_fw_data *)(fw->data +
			2048/sizeof(fw->data[0]));

	for (i = 0; i < fw_size; i++) {
		ret = i2c_smbus_write_i2c_block_data(client, fw_data[i].offset,
						     4, (u8 *)&fw_data[i].val);
		if (ret) {
			dev_err(dev, "Firmware load error %d\n", ret);
			goto release_fw_err;
		}
	}

	return 0;

release_fw_err:
	return ret;
}

static u32 silead_ts_get_status(struct i2c_client *client)
{
	int ret;
	u32 status;

	struct device *dev = &client->dev;
	ret = i2c_smbus_read_i2c_block_data(client, SILEAD_REG_STATUS, 4,
					    (u8 *)&status);
	if (ret < 0) {
		dev_err(&client->dev, "Status read error %d\n", ret);
		return ret;
	}


	return status;
}

static int silead_ts_get_id(struct i2c_client *client)
{
	struct silead_ts_data *data = i2c_get_clientdata(client);
	int ret;

	ret = i2c_smbus_read_i2c_block_data(client, SILEAD_REG_ID, 4,
					    (u8 *)&data->chip_id);
	if (ret < 0) {
		dev_err(&client->dev, "Status read error %d\n", ret);
		return ret;
	}

	return 0;
}

static int silead_ts_setup(struct i2c_client *client)
{
	struct silead_ts_data *data = i2c_get_clientdata(client);
	struct device *dev = &client->dev;
	int ret;
	u32 status;

	silead_ts_set_power(client, SILEAD_POWER_OFF);
	msleep(20);
	silead_ts_set_power(client, SILEAD_POWER_ON);
	msleep(20);

	ret = silead_ts_get_id(client);
	if (ret)
		return ret;

	dev_dbg(dev, "Chip ID: 0x%8X", data->chip_id);

	ret = silead_ts_init(client);
	if (ret)
		return ret;

	ret = silead_ts_reset(client);
	if (ret)
		return ret;

	ret = silead_ts_load_fw(client);
	if (ret)
		return ret;

	ret = silead_ts_startup(client);
	if (ret)
		return ret;

	msleep(20);

	status = silead_ts_get_status(client);

	if (status != SILEAD_STATUS_OK) {
		dev_err(dev, "Initialization error, status: 0x%X\n", status);
	}

	return 0;
}

static irqreturn_t silead_ts_irq_handler(int irq, void *id)
{
	struct silead_ts_data *data = (struct silead_ts_data *)id;
	struct i2c_client *client = data->client;

	silead_ts_read_data(client);

	return IRQ_HANDLED;
}

static int silead_get_acpi_propdata(struct i2c_client *client)
{
	struct silead_ts_data *data = i2c_get_clientdata(client);
	struct acpi_device *adev = ACPI_COMPANION(&client->dev);
	struct acpi_buffer buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	acpi_status status;
	union acpi_object *obj, *elem;

	if (!adev)
		return -ENODEV;

	status = acpi_evaluate_object(adev->handle, "PRP0", NULL, &buffer);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	obj = buffer.pointer;

	if (!obj || obj->type != ACPI_TYPE_PACKAGE || !obj->package.count)
		goto prop_err;

	/* first element is firmware name */
	elem = &obj->package.elements[0];
	if (elem->type != ACPI_TYPE_STRING)
		goto prop_err;

	strlcpy(data->fw_name, elem->string.pointer, SILEAD_FW_NAME_LEN);

	/* second element is x max */
	elem = &obj->package.elements[1];
	if (elem->type != ACPI_TYPE_INTEGER)
		goto prop_err;

	data->x_max = elem->integer.value;

	/* third element is y max */
	elem = &obj->package.elements[2];
	if (elem->type != ACPI_TYPE_INTEGER)
		goto prop_err;

	data->y_max = elem->integer.value;

	/* fourth element is swap axis */
	if (obj->package.count > 3) {
		elem = &obj->package.elements[3];
		if (elem->type != ACPI_TYPE_INTEGER)
			goto prop_err;
		data->xy_swap = elem->integer.value;
	}

	/* fifth element is invert x */
	if (obj->package.count > 4) {
		elem = &obj->package.elements[4];
		if (elem->type != ACPI_TYPE_INTEGER)
			goto prop_err;
		data->x_invert = elem->integer.value;
	}

	/* sixith element is invert y */
	if (obj->package.count > 5) {
		elem = &obj->package.elements[5];
		if (elem->type != ACPI_TYPE_INTEGER)
			goto prop_err;
		data->y_invert = elem->integer.value;
	}

	dev_dbg(&client->dev,
		"acpi fw_name:%s x_max:%d y_max:%d "
		"swap:%d xinvert:%d yinvert:%d\n",
		data->fw_name, data->x_max,
		data->y_max, data->xy_swap,
		data->x_invert, data->y_invert);

	kfree(buffer.pointer);
	return 0;

prop_err:
	kfree(buffer.pointer);
	return -EINVAL;
}

#ifdef CONFIG_PM
static int silead_ts_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	disable_irq(client->irq);
	silead_ts_set_power(client, SILEAD_POWER_OFF);
	msleep(20);
	return 0;
}

static int silead_ts_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	int ret, status;

	/* send power off again, to handle some hardware reset issue */
	silead_ts_set_power(client, SILEAD_POWER_OFF);
	silead_ts_set_power(client, SILEAD_POWER_ON);

	ret = silead_ts_reset(client);
	if (ret) {
		dev_err(dev, "reset error, ret: 0x%X\n", ret);
		return ret;
	}

	ret = silead_ts_startup(client);
	if (ret) {
		dev_err(dev, "start up error, ret: 0x%X\n", ret);
		return ret;
	}

	msleep(20);

	status = silead_ts_get_status(client);
	if (status != SILEAD_STATUS_OK) {
		dev_err(dev, "Resume error, status: 0x%X\n", status);
		return -ENODEV;
	}

	enable_irq(client->irq);

	return 0;
}

static ssize_t silead_power_hal_suspend_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t count)
{
	static DEFINE_MUTEX(mutex);

	mutex_lock(&mutex);
	if (!strncmp(buf, POWER_HAL_SUSPEND_ON, POWER_HAL_SUSPEND_STATUS_LEN))
		silead_ts_suspend(dev);
	else
		silead_ts_resume(dev);
	mutex_unlock(&mutex);

	return count;
}

static DEVICE_POWER_HAL_SUSPEND_ATTR(silead_power_hal_suspend_store);
#endif
static int silead_ts_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	struct silead_ts_data *data;
	struct device *dev = &client->dev;
	int ret;
	static int first_time = 0;
/* loading the firmware to the Silead takes about 4 seconds, so let's defer
 * probing until we are multi-threaded
 */
	if (++first_time == 1)
		return -EPROBE_DEFER;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_I2C |
				     I2C_FUNC_SMBUS_READ_I2C_BLOCK |
				     I2C_FUNC_SMBUS_WRITE_I2C_BLOCK)) {
		dev_err(dev, "I2C functionality check failed\n");
		return -ENXIO;
	}

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	i2c_set_clientdata(client, data);
	data->client = client;

	strncpy(data->fw_name, SILEAD_FW_NAME, SILEAD_FW_NAME_LEN);
	data->x_max = SILEAD_X_MAX;
	data->y_max = SILEAD_Y_MAX;
	data->max_fingers = SILEAD_MAX_FINGERS;
	data->pressure = SILEAD_PRESSURE;

	/* get acpi porperties if available */
	ret = silead_get_acpi_propdata(client);
	if (ret < 0)
		dev_warn(dev, "acpi getting properties failed\n");

	/* If the IRQ is not filled by DT or ACPI subsytem
	 * try using the named GPIO */
	if (client->irq <= 0) {
		data->gpio_irq = devm_gpiod_get(dev, SILEAD_IRQ_GPIO_NAME);
		if (IS_ERR(data->gpio_irq)) {
			dev_err(dev, "IRQ GPIO request failed\n");
			return -ENODEV;
		}

		ret = gpiod_direction_input(data->gpio_irq);
		if (ret) {
			dev_err(dev, "IRQ GPIO direction set failed\n");
			return ret;
		}

		client->irq = gpiod_to_irq(data->gpio_irq);
		if (client->irq <= 0) {
			dev_err(dev, "GPIO to IRQ translation failed %d\n",
				client->irq);
			return client->irq;
		}
	}

	/* Power GPIO pin */
	if (client->dev.of_node) {
		ret = of_get_named_gpio_flags(client->dev.of_node,
					      SILEAD_PWR_GPIO_NAME, 0, NULL);
		if (ret <= 0) {
			dev_err(&client->dev, "error getting gpio for %s\n",
				SILEAD_PWR_GPIO_NAME);
			return -ENODEV;
		}

		data->gpio_power = gpio_to_desc(ret);
		if (!data->gpio_power)
			return -ENODEV;
	} else {
		data->gpio_power = devm_gpiod_get_index(dev,
							SILEAD_PWR_GPIO_NAME,
							1);
		if (IS_ERR(data->gpio_power)) {
			dev_err(dev, ">>>>> POWER GPIO may not be connected\n");
			data->gpio_power = NULL;
		}
	}

	if (data->gpio_power) {
		ret = gpiod_direction_output(data->gpio_power, 0);
		if (ret) {
			dev_err(dev, "Shutdown GPIO direction set failed\n");
			return ret;
		}
	}

	ret = silead_ts_setup(client);
	if (ret)
		return ret;

	ret = silead_ts_request_input_dev(data);
	if (ret)
		return ret;
#ifdef GSL_ALG_ID
	gsl_DataInit(gsl_config_data_id);
#endif
	ret = devm_request_threaded_irq(dev, client->irq, NULL,
					silead_ts_irq_handler,
					IRQF_ONESHOT | IRQ_TYPE_EDGE_RISING,
					client->name, data);
	if (ret) {
		dev_err(dev, "IRQ request failed %d\n", ret);
		return ret;
	}

#ifdef CONFIG_PM
	ret = device_create_file(dev, &dev_attr_power_HAL_suspend);
	if (ret < 0) {
		dev_err(dev, "unable to create suspend entry");
		goto out;
	}

	ret = register_power_hal_suspend_device(dev);
	if (ret < 0)
		dev_err(dev, "unable to register for power hal");
out:
#endif

#ifdef GSL_ALG_ID
	gsl_DataInit(gsl_config_data_id);
#endif
	dev_dbg(dev, "Probing succeded\n");
	return 0;
}

static int silead_ts_remove(struct i2c_client *client)
{
	struct silead_ts_data *data = i2c_get_clientdata(client);

	/* If the IRQ is backed by a GPIO requested in the driver the GPIO will
	 * be release after the driver is removed. In order to force the probe
	 * to re-request the GPIO we will restore the original IRQ value */
	if (data->gpio_irq)
		client->irq = -1;

#ifdef CONFIG_PM
	device_remove_file(&client->dev, &dev_attr_power_HAL_suspend);
	unregister_power_hal_suspend_device(&client->dev);
#endif
	return 0;
}

static const struct i2c_device_id silead_ts_id[] = {
	{ "GSL1680", 0 },
	{ "GSL1688", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, silead_ts_id);

static const struct acpi_device_id silead_ts_acpi_match[] = {
	{ "GSL1680", 0 },
	{ "GSL1688", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, silead_ts_acpi_match);

static struct i2c_driver silead_ts_driver = {
	.probe = silead_ts_probe,
	.remove = silead_ts_remove,
	.id_table = silead_ts_id,
	.driver = {
		.name = SILEAD_TS_NAME,
		.owner = THIS_MODULE,
		.acpi_match_table = ACPI_PTR(silead_ts_acpi_match),
	},
};
module_i2c_driver(silead_ts_driver);

MODULE_AUTHOR("Robert Dolca <robert.dolca@intel.com>");
MODULE_DESCRIPTION("Silead I2C touchscreen driver");
MODULE_LICENSE("GPL");
