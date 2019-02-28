#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/of.h>

#include <linux/leds.h>

#include <linux/mfd/pmic-lorix.h>
#include <linux/mfd/core.h>

#define REG_LAST_RESET_STATE        0x00
#define REG_LED_BRIGTHNESS          0x01
#define REG_FW_VERSION_LENGTH       0x02
#define REG_FW_VERSION              0x03
#define REG_HW_VERSION_LENGTH       0x04
#define REG_HW_VERSION              0x05
#define REG_PRODUCT_NAME_LENGTH     0x06
#define REG_PRODUCT_NAME            0x07
#define REG_PRODUCT_TYPE_LENGTH     0x08
#define REG_PRODUCT_TYPE            0x09
#define REG_FEATURE1                0x0A
#define REG_FEATURE2                0x0B

static struct mfd_cell pmic_lorix_devs[] = {
	{
		.name = "pmic-lorix-led",
		.of_compatible = "wifx,pmic-lorix-led",
	},
};

typedef struct __attribute__((packed, aligned(1))){
    union{
        uint8_t raw;
        struct{
            uint8_t     FEAT_LEGACY : 1;
            uint8_t     FEAT_BOOT   : 1;
            uint8_t     FEAT_FW_VER : 1;
            uint8_t     FEAT_HW_VER : 1;
            uint8_t     FEAT_NAME   : 1;
            uint8_t     FEAT_TYPE   : 1;
            uint8_t     FEAT_PROG   : 1;
            uint8_t     FEAT_UNUSED : 1;
        } bit;
    };
} reg_features1_t;

typedef struct __attribute__((packed, aligned(1))){
    union{
        uint8_t raw;
        struct{
            uint8_t     FEAT_UNUSED : 8;
        } bit;
    };
} reg_features2_t;

static reg_features1_t reg_features1 = { .raw = 0x00 };
static reg_features2_t reg_features2 = { .raw = 0x00 };

static int __lorix_read(struct i2c_client *client,
				int reg, uint8_t *val)
{
	int ret;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0) {
		dev_err(&client->dev, "failed reading at 0x%02x\n", reg);
		return ret;
	}

	*val = (uint8_t)ret;
	return 0;
}

static int __lorix_write(struct i2c_client *client,
				 int reg, uint8_t val)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0) {
		dev_err(&client->dev, "failed writing 0x%02x to 0x%02x\n",
				val, reg);
		return ret;
	}
	return 0;
}

int pmic_lorix_write(struct attiny *attiny, int reg, uint8_t val)
{
	int ret;
	mutex_lock(&attiny->lock);

	ret = __lorix_write(attiny->client, reg, val);

	mutex_unlock(&attiny->lock);
	return ret;
}
EXPORT_SYMBOL(pmic_lorix_write);

int pmic_lorix_read(struct attiny *attiny, int reg, uint8_t *val)
{
	int ret;
	mutex_lock(&attiny->lock);

	ret = __lorix_read(attiny->client, reg, val);

	mutex_unlock(&attiny->lock);
	return ret;
}
EXPORT_SYMBOL(pmic_lorix_read);

static int boot_state_get(struct attiny *attiny, uint8_t *boot_state)
{
	int ret = pmic_lorix_read(attiny, REG_LAST_RESET_STATE, boot_state);
	if (ret < 0)
		*boot_state = 0xFF;

	return ret;
}

static int boot_state_clr(struct attiny *attiny)
{
    return pmic_lorix_write(attiny, REG_LAST_RESET_STATE, 0xFF);
}

static int fw_version_get(struct attiny *attiny, char *str)
{
	int ret;
	uint8_t len;
	char tmp[10];

	mutex_lock(&attiny->lock);

	ret = i2c_smbus_read_byte_data(attiny->client, REG_FW_VERSION_LENGTH);
	if (ret < 0) {
		dev_err(attiny->dev, "failed reading register FW_VERSION_LENGTH\n");
		goto out_err_read;
	}
	len = (uint8_t)ret;
	if (len > 10) {
		dev_err(attiny->dev, "error with FW version length (length read = %d)\n", len);
		goto out_err_read;
	}

	ret = i2c_smbus_read_i2c_block_data(attiny->client, REG_FW_VERSION, (uint8_t)ret, tmp);
	if (ret < 0) {
		dev_err(attiny->dev, "failed reading register FW_VERSION\n");
		goto out_err_read;
	}

	mutex_unlock(&attiny->lock);

	strncpy(str, tmp, len);
	return (int)len;

out_err_read:
	dev_err(attiny->dev, "failed retrieving FW version\n");
	mutex_unlock(&attiny->lock);
	return -EIO;
}

static int hw_version_get(struct attiny *attiny, char *str)
{
	int ret;
	uint8_t len;
	char tmp[10];

	mutex_lock(&attiny->lock);

	ret = i2c_smbus_read_byte_data(attiny->client, REG_HW_VERSION_LENGTH);
	if (ret < 0) {
		dev_err(attiny->dev, "failed reading register HW_VERSION_LENGTH\n");
		goto out_err_read;
	}
	len = (uint8_t)ret;
	if (len > 10) {
		dev_err(attiny->dev, "error with HW version length (length read = %d)\n", len);
		goto out_err_read;
	}

	ret = i2c_smbus_read_i2c_block_data(attiny->client, REG_HW_VERSION, (uint8_t)ret, tmp);
	if (ret < 0) {
		dev_err(attiny->dev, "failed reading register HW_VERSION\n");
		goto out_err_read;
	}

	mutex_unlock(&attiny->lock);

	strncpy(str, tmp, len);
	return (int)len;

out_err_read:
	dev_err(attiny->dev, "failed retrieving HW version\n");
	mutex_unlock(&attiny->lock);
	return -EIO;
}

static int product_name_get(struct attiny *attiny, char *str)
{
	int ret;
	uint8_t len;
	char tmp[16];

	mutex_lock(&attiny->lock);

	ret = i2c_smbus_read_byte_data(attiny->client, REG_PRODUCT_NAME_LENGTH);
	if (ret < 0) {
		dev_err(attiny->dev, "failed reading register PRODUCT_NAME_LENGTH\n");
		goto out_err_read;
	}
	len = (uint8_t)ret;
	if (len > 16) {
		dev_err(attiny->dev, "error with product name length (length read = %d)\n", len);
		goto out_err_read;
	}

	ret = i2c_smbus_read_i2c_block_data(attiny->client, REG_PRODUCT_NAME, (uint8_t)ret, tmp);
	if (ret < 0) {
		dev_err(attiny->dev, "failed reading register PRODUCT_NAME\n");
		goto out_err_read;
	}

	mutex_unlock(&attiny->lock);

	strncpy(str, tmp, len);
	return (int)len;

out_err_read:
	dev_err(attiny->dev, "failed retrieving product name\n");
	mutex_unlock(&attiny->lock);
	return -EIO;
}

static int product_type_get(struct attiny *attiny, char *str)
{
    int ret;
    uint8_t len;
    char tmp[16];

    mutex_lock(&attiny->lock);

    ret = i2c_smbus_read_byte_data(attiny->client, REG_PRODUCT_TYPE_LENGTH);
    if (ret < 0) {
        dev_err(attiny->dev, "failed reading register PRODUCT_TYPE_LENGTH\n");
        goto out_err_read;
    }
    len = (uint8_t)ret;
    if (len > 16) {
        dev_err(attiny->dev, "error with product type length (length read = %d)\n", len);
        goto out_err_read;
    }

    ret = i2c_smbus_read_i2c_block_data(attiny->client, REG_PRODUCT_TYPE, (uint8_t)ret, tmp);
    if (ret < 0) {
        dev_err(attiny->dev, "failed reading register PRODUCT_TYPE\n");
        goto out_err_read;
    }

    mutex_unlock(&attiny->lock);

    strncpy(str, tmp, len);
    return (int)len;

out_err_read:
    dev_err(attiny->dev, "failed retrieving product type\n");
    mutex_unlock(&attiny->lock);
    return -EIO;
}

static int reg_feature1_get(struct attiny *attiny, reg_features1_t *reg_feature1)
{
    int ret = pmic_lorix_read(attiny, REG_FEATURE1, &reg_feature1->raw);
    if (ret < 0)
        reg_feature1->raw = 0x00;

    return ret;
}

static int reg_feature2_get(struct attiny *attiny, reg_features2_t *reg_feature2)
{
    int ret = pmic_lorix_read(attiny, REG_FEATURE2, &reg_feature2->raw);
    if (ret < 0)
        reg_feature2->raw = 0x00;

    return ret;
}

static ssize_t boot_state_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct attiny *attiny = dev_get_drvdata(dev);
	uint8_t boot_state;

	boot_state_get(attiny, &boot_state);
	return sprintf(buf, "%d\n", boot_state);
}

static ssize_t boot_state_store(struct device *dev,
                struct device_attribute *attr,
                const char *buf, size_t count)
{
    struct attiny *attiny = dev_get_drvdata(dev);
    int inval;

    sscanf(buf, "%du", &inval);
    if(inval != 0){
        boot_state_clr(attiny);
    }
    return count;
}

static ssize_t fw_version_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct attiny *attiny = dev_get_drvdata(dev);
	char fw_ver[10];
	int ret;

	ret = fw_version_get(attiny, fw_ver);
	if (ret < 0) {
		return sprintf(buf, "unknown\n");
	} else {
		return sprintf(buf, "%s\n", fw_ver);
	}
}

static ssize_t hw_version_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct attiny *attiny = dev_get_drvdata(dev);
	char hw_ver[10];
	int ret;

	ret = hw_version_get(attiny, hw_ver);
	if (ret < 0) {
		return sprintf(buf, "unknown\n");
	} else {
		return sprintf(buf, "%s\n", hw_ver);
	}
}

static ssize_t product_name_show(struct device *dev,
                struct device_attribute *attr, char *buf)
{
    struct attiny *attiny = dev_get_drvdata(dev);
    char name[16];
    int ret;

    ret = product_name_get(attiny, name);
    if (ret < 0) {
        return sprintf(buf, "unknown\n");
    } else {
        return sprintf(buf, "%s\n", name);
    }
}

static ssize_t product_type_show(struct device *dev,
                struct device_attribute *attr, char *buf)
{
    struct attiny *attiny = dev_get_drvdata(dev);
    char type[16];
    int ret;

    ret = product_type_get(attiny, type);
    if (ret < 0) {
        return sprintf(buf, "unknown\n");
    } else {
        return sprintf(buf, "%s\n", type);
    }
}

static DEVICE_ATTR(boot_state, (S_IWUSR|S_IRUSR|S_IWGRP|S_IRGRP|S_IROTH), boot_state_show, boot_state_store);
static DEVICE_ATTR(fw_version, S_IRUGO, fw_version_show, NULL);
static DEVICE_ATTR(hw_version, S_IRUGO, hw_version_show, NULL);
static DEVICE_ATTR(product_name, S_IRUGO, product_name_show, NULL);
static DEVICE_ATTR(product_type, S_IRUGO, product_type_show, NULL);

static int pmic_lorix_probe(struct i2c_client *client,
							const struct i2c_device_id *id)
{

	struct attiny_platform_data *pdata = dev_get_platdata(&client->dev);
	//struct platform_device *pdev;
	struct attiny *attiny;
	struct device *dev = &client->dev;
	int ret;
	uint8_t boot_state;
	char fw_ver[10], hw_ver[10], name[16], type[16];
	char *ptr;

	if (!i2c_check_functionality(client->adapter,
					I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(dev, "SMBUS Word Data not Supported\n");
		return -EIO;
	}

	// creating the driver data
	attiny = devm_kzalloc(&client->dev, sizeof(*attiny), GFP_KERNEL);
	if (!attiny) {
		dev_err(dev, "Memory allocation failed\n");
		return -ENOMEM;
	}

	// assign driver data to device
	dev_set_drvdata(&client->dev, attiny);
	attiny->client = client;
	attiny->dev = &client->dev;
	attiny->id = id->driver_data;

	// init the device lock
	mutex_init(&attiny->lock);

	if (pdata) {
		pmic_lorix_devs[0].platform_data = &pdata->leds;
		pmic_lorix_devs[0].pdata_size = sizeof(pdata->leds);
	} else {
		pmic_lorix_devs[0].platform_data = NULL;
		pmic_lorix_devs[0].pdata_size = 0;
	}
	ret = mfd_add_devices(&client->dev, -1, (const struct mfd_cell *)&pmic_lorix_devs, 1, NULL, 0, NULL);

	if (ret < 0) {
		dev_err(&client->dev, "add mfd devices failed: %d\n", ret);
		return ret;
	}

	// create sysfs entry
	ret = device_create_file(&client->dev, &dev_attr_boot_state);
	if (ret < 0) {
		dev_err(&client->dev, "failed to add BOOT_STATE sysfs file\n");
		goto out_remove_device;
	}
	ret = device_create_file(&client->dev, &dev_attr_fw_version);
	if (ret < 0) {
		dev_err(&client->dev, "failed to add FW_VERSION sysfs file\n");
		goto out_remove_file_boot_state;
	}
	ret = device_create_file(&client->dev, &dev_attr_hw_version);
	if (ret < 0) {
		dev_err(&client->dev, "failed to add HW_VERSION sysfs file\n");
		goto out_remove_file_fw_version;
	}
	ret = device_create_file(&client->dev, &dev_attr_product_name);
    if (ret < 0) {
        dev_err(&client->dev, "failed to add NAME sysfs file\n");
        goto out_remove_file_hw_version;
    }
    ret = device_create_file(&client->dev, &dev_attr_product_type);
    if (ret < 0) {
        dev_err(&client->dev, "failed to add TYPE sysfs file\n");
        goto out_remove_file_product_name;
    }

    // FW version, HW version and boot state work in all cases since revision 1.0c

	// read FW version
	ret = fw_version_get(attiny, fw_ver);
	if (ret < 0) {
	    dev_err(&client->dev, "failed to retrieve FW version from pmic-lorix\n");
		goto out_remove_all;
	}

	// read bootstate
	ret = boot_state_get(attiny, &boot_state);
	if (ret < 0) {
		dev_err(&client->dev, "failed to retrieve boot_state from pmic-lorix\n");
		goto out_remove_all;
	}

	// read HW version
	ret = hw_version_get(attiny, hw_ver);
	if (ret < 0) {
	    dev_err(&client->dev, "failed to retrieve HW version from pmic-lorix\n");
		goto out_remove_all;
	}

	// values by default
	ptr = strcpy(name, "LORIX One");
	ptr = strcpy(type, "EU868");

	// test if features regs can be read
	ret = reg_feature1_get(attiny, &reg_features1);
	if (ret < 0 || reg_features1.bit.FEAT_LEGACY) {
	    reg_features1.bit.FEAT_BOOT = 1;
	    reg_features1.bit.FEAT_FW_VER = 1;
	    reg_features1.bit.FEAT_HW_VER = 1;
	    reg_features1.bit.FEAT_NAME = 0;
        reg_features1.bit.FEAT_TYPE = 0;
        reg_features1.bit.FEAT_PROG = 0;
	}

	// unused actually
	ret = reg_feature2_get(attiny, &reg_features2);

	// read product name if possible
	if (reg_features1.bit.FEAT_NAME) {
	    ret = product_name_get(attiny, name);
        if (ret < 0) {
            dev_err(&client->dev, "failed to retrieve product name from pmic-lorix\n");
            goto out_remove_all;
        }
	}

	// read product type if possible
    if (reg_features1.bit.FEAT_TYPE) {
        ret = product_type_get(attiny, type);
        if (ret < 0) {
            dev_err(&client->dev, "failed to retrieve product type from pmic-lorix\n");
            goto out_remove_all;
        }
    }

display_infos:
    dev_info(&client->dev, "Product %s detected\n", name);
    dev_info(&client->dev, "   Type: %s\n", type);
    dev_info(&client->dev, " HW ver: %s\n", hw_ver);
    dev_info(&client->dev, " FW ver: %s\n", fw_ver);

    switch(boot_state){
    case 0x00:
        dev_info(&client->dev, "   Boot: 0x00 (normal mode)\n");
        break;
    case 0x01:
        dev_info(&client->dev, "   Boot: 0x01 (factory reset mode)\n");
        break;
    default:
        dev_info(&client->dev, "   Boot: 0x%02X (unknown mode)\n", boot_state);
        break;
    }

	return 0;

out_remove_all:
out_remove_file_product_type:
    device_remove_file(&client->dev, &dev_attr_product_type);

out_remove_file_product_name:
    device_remove_file(&client->dev, &dev_attr_product_name);

out_remove_file_hw_version:
	device_remove_file(&client->dev, &dev_attr_hw_version);

out_remove_file_fw_version:
	device_remove_file(&client->dev, &dev_attr_fw_version);

out_remove_file_boot_state:
	device_remove_file(&client->dev, &dev_attr_boot_state);

out_remove_device:
	mfd_remove_devices(&client->dev);

	return ret;
}

static int pmic_lorix_remove(struct i2c_client *client)
{
	device_remove_file(&client->dev, &dev_attr_hw_version);
	device_remove_file(&client->dev, &dev_attr_fw_version);
	device_remove_file(&client->dev, &dev_attr_boot_state);
	mfd_remove_devices(&client->dev);

	return 0;
}

static const struct i2c_device_id pmic_lorix_id[] = {
	{"pmic-lorix", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, pmic_lorix_id);

#ifdef CONFIG_OF
static const struct of_device_id pmic_lorix_of_match[] = {
	{ .compatible = "wifx,pmic-lorix", },
	{},
};
MODULE_DEVICE_TABLE(of, pmic_lorix_of_match);
#endif

static struct i2c_driver pmic_lorix_driver = {
	.probe		= pmic_lorix_probe,
	.remove		= pmic_lorix_remove,
	.driver = {
		.name	= "pmic-lorix",
		.of_match_table = of_match_ptr(pmic_lorix_of_match),
	},
	.id_table 	= pmic_lorix_id,
};
//module_i2c_driver(pmic_lorix_driver);

static int __init pmic_lorix_i2c_init(void)
{
	return i2c_add_driver(&pmic_lorix_driver);
}
/* init early so consumer devices can complete system boot */
subsys_initcall(pmic_lorix_i2c_init);

static void __exit pmic_lorix_i2c_exit(void)
{
	i2c_del_driver(&pmic_lorix_driver);
}
module_exit(pmic_lorix_i2c_exit);

MODULE_AUTHOR("Yannick Lanz <yannick.lanz@wifx.net>");
MODULE_DESCRIPTION("LORIX One PMIC-MFD Driver");
MODULE_LICENSE("GPL");
