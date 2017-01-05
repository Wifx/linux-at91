#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/of.h>
#include <linux/mfd/pmic-lorix.h>

#define LORIX_DISP_LED_NAME		"status_led"

struct attiny_led {
	struct led_classdev	cdev;
	struct work_struct	work;
	enum led_brightness	new_brightness;
	int					id;
	struct attiny_leds	*leds;
};

struct attiny_leds {
	struct attiny		*master;
	int					num_leds;
	struct attiny_led	*led;
};

static void attiny_led_set_work(struct work_struct *work)
{
	struct attiny_led *led =
			container_of(work, struct attiny_led, work);
	struct attiny_leds *leds = led->leds;

	pmic_lorix_write(leds->master, 0x01, led->new_brightness);
}

static void attiny_led_set(struct led_classdev *led_cdev,
				enum led_brightness value)
{
	struct attiny_led *led =
			container_of(led_cdev, struct attiny_led, cdev);

	led->new_brightness = value;
	schedule_work(&led->work);
}

#ifdef CONFIG_OF
static struct attiny_leds_platform_data __init *attiny_led_probe_dt(
	struct platform_device *pdev)
{
	struct attiny_leds_platform_data *pdata;
	struct device_node *parent, *child;
	struct device *dev = &pdev->dev;
	int i = 0, ret = -ENODATA;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	parent = of_get_child_by_name(dev->parent->of_node, "leds");
	if (!parent)
		goto out_node_put;

	ret = of_property_read_u32_array(parent, "led-control",
					 pdata->led_control,
					 1);
	if (ret)
		goto out_node_put;

	pdata->num_leds = of_get_child_count(parent);

	pdata->led = devm_kzalloc(dev, pdata->num_leds * sizeof(*pdata->led),
				  GFP_KERNEL);
	if (!pdata->led) {
		ret = -ENOMEM;
		goto out_node_put;
	}

	for_each_child_of_node(parent, child) {
		const char *str;
		u32 tmp;

		if (of_property_read_u32(child, "reg", &tmp))
			continue;
		pdata->led[i].id = 0 + tmp;

		if (!of_property_read_string(child, "label", &str))
			pdata->led[i].name = str;
		if (!of_property_read_string(child, "linux,default-trigger",
					     &str))
			pdata->led[i].default_trigger = str;

		i++;
	}

	pdata->num_leds = i;
	ret = i > 0 ? 0 : -ENODATA;

out_node_put:
	of_node_put(parent);

	return ret ? ERR_PTR(ret) : pdata;
}
#else
static inline struct attiny_leds_platform_data __init *attiny_led_probe_dt(
	struct platform_device *pdev)
{
	return ERR_PTR(-ENOSYS);
}
#endif

static int __init attiny_led_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct attiny_leds_platform_data *pdata = dev_get_platdata(dev);
	struct attiny *atdev = dev_get_drvdata(dev->parent);
	struct attiny_leds *leds;
	int i, id, ret = -ENODATA;
	u32 init_led = 0;

	leds = devm_kzalloc(dev, sizeof(*leds), GFP_KERNEL);
	if(!leds)
		return -ENOMEM;

	leds->master = atdev;
	platform_set_drvdata(pdev, leds);

	if(dev->parent->of_node) {
		pdata = attiny_led_probe_dt(pdev);
		if (IS_ERR(pdata))
			return PTR_ERR(pdata);
	} else if (!pdata)
		return -ENODATA;

	leds->num_leds = pdata->num_leds;

	/*if ((leds->num_leds < 1) ||
		(leds->num_leds > (devtype->led_max - devtype->led_min + 1))) {
		dev_err(dev, "Invalid LED count %d\n", leds->num_leds);
		return -EINVAL;
	}*/

	leds->led = devm_kzalloc(dev, leds->num_leds * sizeof(*leds->led),
				 GFP_KERNEL);
	if (!leds->led)
		return -ENOMEM;

	for (i = 0; i < 1; i++) {
		/*ret = mc13xxx_reg_write(atdev, leds->devtype->ledctrl_base + i,
					pdata->led_control[i]);
		*/
		ret = 0;

		if (ret)
			return ret;
	}

	for (i = 0; i < leds->num_leds; i++) {
		const char *name, *trig;

		ret = -EINVAL;

		id = pdata->led[i].id;
		name = pdata->led[i].name;
		trig = pdata->led[i].default_trigger;

		if ((id > 1) || (id < 0)) {
			dev_err(dev, "Invalid ID %i\n", id);
			break;
		}

		if (init_led & (1 << id)) {
			dev_warn(dev, "LED %i already initialized\n", id);
			break;
		}

		init_led |= 1 << id;
		leds->led[i].id = id;
		leds->led[i].leds = leds;
		leds->led[i].cdev.name = name;
		leds->led[i].cdev.default_trigger = trig;
		leds->led[i].cdev.flags = LED_CORE_SUSPENDRESUME;
		leds->led[i].cdev.brightness_set = attiny_led_set;
		leds->led[i].cdev.max_brightness = 255;

		INIT_WORK(&leds->led[i].work, attiny_led_set_work);

		ret = led_classdev_register(dev->parent, &leds->led[i].cdev);
		if (ret) {
			dev_err(dev, "Failed to register LED %i\n", id);
			break;
		}else{
			dev_info(dev, "registred led (name = %s, trigger = %s)\n", name, trig);
		}
	}

	if (ret)
		while (--i >= 0) {
			led_classdev_unregister(&leds->led[i].cdev);
			cancel_work_sync(&leds->led[i].work);
		}

	return ret;
}

static int attiny_led_remove(struct platform_device *pdev)
{
	struct attiny_leds *leds = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < leds->num_leds; i++) {
		led_classdev_unregister(&leds->led[i].cdev);
		cancel_work_sync(&leds->led[i].work);
	}

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id pmic_lorix_led_of_match[] = {
	{ .compatible = "wifx,pmic-lorix-led", },
	{},
};
MODULE_DEVICE_TABLE(of, pmic_lorix_led_of_match);
#endif

static struct platform_driver attiny_led_driver = {
	.driver = {
		.name	= "pmic-lorix-led",
		.of_match_table = of_match_ptr(pmic_lorix_led_of_match),
	},
	.remove   = attiny_led_remove,
};

module_platform_driver_probe(attiny_led_driver, attiny_led_probe);

MODULE_AUTHOR("Yannick Lanz <yannick.lanz@wifx.net>");
MODULE_DESCRIPTION("LORIX One Status LED");
MODULE_LICENSE("GPL");
