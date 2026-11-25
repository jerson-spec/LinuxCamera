/*
 * 修改：AP3216C 环境光驱动（IIO）。
 *
 * 设备树匹配 "linuxcamera,ap3216c"，挂在 21a0000.i2c
 * （用户态 /dev/i2c-0）地址 0x1e。
 *
 * 注册成一个 IIO 设备，只暴露环境光：
 *   /sys/bus/iio/devices/iio:deviceN/in_illuminance_input   单位 lux
 *
 * 边界：驱动只做"读寄存器 + 换算 lux + 上报"。
 * 阈值、迟滞、亮度档位都在用户态 autolight.c，改策略不用重编内核。
 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/bitops.h>	/* 修改：BIT() */

#define AP3216C_REG_SYS_CONF	0x00
#define AP3216C_REG_ALS_L	0x0C
#define AP3216C_REG_ALS_H	0x0D

struct ap3216c_data {
	struct i2c_client *client;
};

/*
 * 读 ALS 原始值。和用户态 A 阶段的口径完全一致：
 * 高字节 bit7 是有效标志，数据在低 7 位。
 * 两次读之间被 ALS 刷新的话可能差 1 个计数，对亮度判断没影响。
 */
static int ap3216c_read_als(struct i2c_client *client, int *als)
{
	int lo, hi;

	lo = i2c_smbus_read_byte_data(client, AP3216C_REG_ALS_L);
	if (lo < 0)
		return lo;

	hi = i2c_smbus_read_byte_data(client, AP3216C_REG_ALS_H);
	if (hi < 0)
		return hi;

	*als = ((hi & 0x7F) << 8) | (lo & 0xFF);

	return 0;
}

static int ap3216c_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct ap3216c_data *data = iio_priv(indio_dev);
	int als, ret;

	/* 只提供 processed 值，raw/scale 这一版不开 */
	if (mask != IIO_CHAN_INFO_PROCESSED)
		return -EINVAL;

	ret = ap3216c_read_als(data->client, &als);
	if (ret < 0)
		return ret;

	/* lux = ALS * 0.35，整数实现，内核里不用浮点 */
	*val = als * 35 / 100;

	return IIO_VAL_INT;
}

static const struct iio_info ap3216c_info = {
	.read_raw = ap3216c_read_raw,
};

static const struct iio_chan_spec ap3216c_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
		.channel = 0,
	},
};

static int ap3216c_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct ap3216c_data *data;
	int ret;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev, "adapter has no SMBUS byte data\n");
		return -EOPNOTSUPP;
	}

	/*
	 * devm_iio_device_alloc 分配的是"框架结构 + 私有区"一整块，
	 * 返回值是框架指针；私有区靠 iio_priv() 取。
	 */
	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (indio_dev == NULL)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->client = client;
	i2c_set_clientdata(client, indio_dev);

	/* 和用户态 A 阶段一致：0x00 写 0x03，ALS + PS + IR 使能 */
	ret = i2c_smbus_write_byte_data(client, AP3216C_REG_SYS_CONF, 0x03);
	if (ret < 0) {
		dev_err(&client->dev, "init AP3216C failed: %d\n", ret);
		return ret;
	}

	/* ALS 上电后要一点时间稳定，省了会读到 0 */
	msleep(20);

	indio_dev->dev.parent = &client->dev;
	indio_dev->name = "ap3216c";		/* 用户态按名字找节点 */
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &ap3216c_info;
	indio_dev->channels = ap3216c_channels;
	indio_dev->num_channels = ARRAY_SIZE(ap3216c_channels);

	ret = devm_iio_device_register(&client->dev, indio_dev);
	if (ret < 0) {
		dev_err(&client->dev, "iio_device_register failed: %d\n", ret);
		return ret;
	}

	dev_info(&client->dev, "AP3216C light sensor registered\n");

	return 0;
}

static const struct of_device_id ap3216c_of_match[] = {
	{ .compatible = "linuxcamera,ap3216c" },
	{ }
};
MODULE_DEVICE_TABLE(of, ap3216c_of_match);

static const struct i2c_device_id ap3216c_id[] = {
	{ "ap3216c", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ap3216c_id);

static struct i2c_driver ap3216c_driver = {
	.driver = {
		.name		= "ap3216c",
		.of_match_table	= ap3216c_of_match,
	},
	.probe		= ap3216c_probe,
	.id_table	= ap3216c_id,
};

module_i2c_driver(ap3216c_driver);

MODULE_AUTHOR("LinuxCamera");
MODULE_DESCRIPTION("AP3216C ambient light sensor (IIO)");
MODULE_LICENSE("GPL");
