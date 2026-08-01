
#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/version.h>

#define REG_ID		0x01
#define REG_POWERON	0x02
#define REG_PWM		0x03

#define LCD_RESET_BIT	BIT(0)
#define CTP_RESET_BIT	BIT(1)

#define PWM_BL_ENABLE	BIT(7)
#define PWM_VALUE	GENMASK(4, 0)

#define NUM_GPIO	2	// LCD_RESET, CTP_RESET

struct osoyoo_panel_lcd {
	struct mutex lock;
	struct regmap *regmap;
	u8 poweron_state;

	struct gpio_chip gc;
};

static const struct regmap_config osoyoo_panel_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = REG_PWM,
};

static int osoyoo_panel_gpio_get_direction(struct gpio_chip *gc, unsigned int off)
{
	return GPIO_LINE_DIRECTION_OUT;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 17, 0)
static int osoyoo_panel_gpio_set(struct gpio_chip *gc, unsigned int off, int val)
#else
static void osoyoo_panel_gpio_set(struct gpio_chip *gc, unsigned int off, int val)
#endif
{
	struct osoyoo_panel_lcd *state = gpiochip_get_data(gc);
	u8 last_val;

	if (off >= NUM_GPIO)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 17, 0)
		return -EINVAL;
#else
		return;
#endif

	mutex_lock(&state->lock);

	last_val = state->poweron_state;
	if (val)
		last_val |= (1 << off);
	else
		last_val &= ~(1 << off);

	state->poweron_state = last_val;

	regmap_write(state->regmap, REG_POWERON, last_val);

	mutex_unlock(&state->lock);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 17, 0)
	return 0;
#endif
}

static int osoyoo_panel_update_status(struct backlight_device *bl)
{
	struct regmap *regmap = bl_get_data(bl);
	int brightness = bl->props.brightness;

	if (bl->props.power != FB_BLANK_UNBLANK || bl->props.state & (BL_CORE_SUSPENDED | BL_CORE_FBBLANK))
		brightness = 0;

	return regmap_write(regmap, REG_PWM, brightness | PWM_BL_ENABLE);
}

static const struct backlight_ops osoyoo_panel_bl = {
	.update_status = osoyoo_panel_update_status,
};

static int osoyoo_panel_i2c_read(struct i2c_client *client, u8 reg, unsigned int *buf)
{
	struct i2c_msg msgs[1];
	u8 addr_buf[1] = { reg };
	u8 data_buf[1] = { 0, };
	int ret;

	// Write register address
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = ARRAY_SIZE(addr_buf);
	msgs[0].buf = addr_buf;

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	usleep_range(5000, 10000);

	// Read data from register
	msgs[0].addr = client->addr;
	msgs[0].flags = I2C_M_RD;
	msgs[0].len = 1;
	msgs[0].buf = data_buf;

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*buf = data_buf[0];
	return 0;
}

static int osoyoo_panel_i2c_probe(struct i2c_client *i2c)
{
	struct backlight_properties props = { };
	struct backlight_device *bl;
	struct osoyoo_panel_lcd *state;
	struct regmap *regmap;
	unsigned int data;
	int ret;

	state = devm_kzalloc(&i2c->dev, sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	mutex_init(&state->lock);
	i2c_set_clientdata(i2c, state);

	regmap = devm_regmap_init_i2c(i2c, &osoyoo_panel_regmap_config);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n", ret);
		goto error;
	}

	ret = osoyoo_panel_i2c_read(i2c, REG_ID, &data);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to read id: %d\n", ret);
		goto error;
	}

	regmap_write(regmap, REG_POWERON, 0);

	state->regmap = regmap;
	state->gc.parent = &i2c->dev;
	state->gc.label = i2c->name;
	state->gc.owner = THIS_MODULE;
	state->gc.base = -1;
	state->gc.ngpio = NUM_GPIO;

	state->gc.set = osoyoo_panel_gpio_set;
	state->gc.get_direction = osoyoo_panel_gpio_get_direction;
	state->gc.can_sleep = true;

	ret = devm_gpiochip_add_data(&i2c->dev, &state->gc, state);
	if (ret) {
		dev_err(&i2c->dev, "Failed to create gpiochip: %d\n", ret);
		goto error;
	}

	props.type = BACKLIGHT_RAW;
	props.max_brightness = PWM_VALUE;
	bl = devm_backlight_device_register(&i2c->dev, dev_name(&i2c->dev),
					    &i2c->dev, regmap, &osoyoo_panel_bl,
					    &props);
	if (IS_ERR(bl))
		return PTR_ERR(bl);

	bl->props.brightness = PWM_VALUE;

	return 0;

error:
	mutex_destroy(&state->lock);
	return ret;
}

static void osoyoo_panel_i2c_remove(struct i2c_client *client)
{
	struct osoyoo_panel_lcd *state = i2c_get_clientdata(client);

	mutex_destroy(&state->lock);
}

static void osoyoo_panel_i2c_shutdown(struct i2c_client *client)
{
	struct osoyoo_panel_lcd *state = i2c_get_clientdata(client);

	regmap_write(state->regmap, REG_PWM, 0);
	regmap_write(state->regmap, REG_POWERON, 0);
}

static const struct of_device_id osoyoo_panel_dt_ids[] = {
	{ .compatible = "osoyoo,touchscreen-panel-regulator" },
	{},
};
MODULE_DEVICE_TABLE(of, osoyoo_panel_dt_ids);

static struct i2c_driver osoyoo_panel_regulator_driver = {
	.driver = {
		.name = "osoyoo_touchscreen",
		.of_match_table = of_match_ptr(osoyoo_panel_dt_ids),
	},
	.probe = osoyoo_panel_i2c_probe,
	.remove	= osoyoo_panel_i2c_remove,
	.shutdown = osoyoo_panel_i2c_shutdown,
};

module_i2c_driver(osoyoo_panel_regulator_driver);

MODULE_AUTHOR("OSOYOO");
MODULE_DESCRIPTION("Osoyoo touchscreen regulator device driver");
MODULE_LICENSE("GPL");
