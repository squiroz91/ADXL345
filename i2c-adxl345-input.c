/*
 * =====================================================================================
 *
 *       Filename:  i2c-adxl345.c
 *
 *    Description:  Driver for ADXL345
 *
 *        Version:  1.0
 *        Created:  17/11/2015 14:23:14
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Tycho Tatitscheff
 *   Organization:  ENSAM
 *
 * =====================================================================================
 */


#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/uaccess.h>

#define DRV_NAME        "adxl345"

#define ADXL_DEVID      0x00

// Data
#define ADXL_OFSX       0x1E
#define ADXL_OFSY       0x1F
#define ADXL_OFSZ       0x20

#define ADXL_DATAX0     0x32
#define ADXL_DATAX1     0x33
#define ADXL_DATAY0     0x34
#define ADXL_DATAY1     0x35
#define ADXL_DATAZ0     0x36
#define ADXL_DATAZ1     0x37

// Configuration
#define ADXL_BW         0x2C  // frequency OxOA = 100 Mhz
#define ADXL_INT_ENABLE     0x2E  // desactive les interuptions si mis à 0
#define ADXL_FIFO_MODE      0x38  // deactive la fifo si mis à 0
#define ADXL_DATA_FORMAT    0x31
#define ADXL_POWER_CTL      0x2D  // cf doc

#define ADXL_MAX_X      4095
#define ADXL_MAX_Y      4095
#define ADXL_MAX_Z      4095

struct  adxl345 {
    struct input_dev *input;
    struct i2c_client *client;
    struct adxl345_data *last_data;
};

struct adxl345_data {
    s8 x0;
    s8 x1;
    s8 y0;
    s8 y1;
    s8 z0;
    s8 z1;
};

static void write8(struct i2c_client *client, char reg, char value) {
    char buf[2];
    buf[0] = reg;
    buf[1] = value;
    i2c_master_send(client, buf, 2);
}

static void infodata(struct i2c_client *client, char reg, struct adxl345_data *data_struct) {
    struct i2c_msg msg[] = {
        {
            .addr = client->addr,
            .flags = 0x00,
            .len = 1,
            .buf = &reg
        }, {
            .addr = client->addr,
            .flags = I2C_M_RD,
            .len = 6,
            .buf = (char *) data_struct
        }
    };
    i2c_transfer(client->adapter, msg, 2);
}

// bien aidé par drivers/input/touchscreen/ar1021_i2c.c
static irqreturn_t adxl345_irq(int irq, void *_pwr) {
    struct adxl345 *adxl = _pwr;
    struct i2c_client *client = adxl->client;
    struct input_dev *input = adxl->input;
    s16 x, y, z = 0;

    struct adxl345_data *new_data;
    new_data = devm_kzalloc(&client->dev, sizeof(*adxl), GFP_KERNEL);
    if (!new_data)
        return -ENOMEM;

    infodata(client, ADXL_DATAX0, new_data);

    if (adxl->last_data != new_data) {
        x = new_data->x0 | new_data->x1 << 8;
        input_report_abs(input, ABS_X, x);
        y = new_data->y0 | new_data->y1 << 8;
        input_report_abs(input, ABS_Y, y);
        z = new_data->z0 | new_data->z1 << 8;
        input_report_abs(input, ABS_Z, z);
        input_sync(input);
        adxl->last_data = new_data;
    }
    return IRQ_HANDLED;
}

// bien aidé par drivers/input/touchscreen/ar1021_i2c.c
static int adxl_open(struct input_dev *dev)
{
    struct adxl345 *adxl = input_get_drvdata(dev);
    struct i2c_client *client = adxl->client;
    enable_irq(client->irq);
    return 0;
}

// bien aidé par drivers/input/touchscreen/ar1021_i2c.c
static void adxl_close(struct input_dev *dev)
{
    struct adxl345 *adxl = input_get_drvdata(dev);
    struct i2c_client *client = adxl->client;
    disable_irq(client->irq);
}

// bien aidé par drivers/input/touchscreen/ar1021_i2c.c
static int adxl345_probe(struct i2c_client *client,
                const struct i2c_device_id *id){
    struct adxl345 *adxl;
    struct input_dev *input;
    int error;

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        dev_err(&client->dev, "I2C Check functionality Error\n");
        return -ENXIO;
    }

    adxl = devm_kzalloc(&client->dev, sizeof(struct adxl345), GFP_KERNEL);
    if (!adxl)
        return -ENOMEM;

    input = devm_input_allocate_device(&client->dev);
    if (!input)
        return -ENOMEM;

    adxl->client = client;
    adxl->input = input;

    input->name = "adxl345 I2C Accelerometer";
    input->id.bustype = BUS_I2C;
    input->dev.parent = &client->dev;
    input->open = adxl_open;
    input->close = adxl_close;

    input_set_abs_params(input, ABS_X, - ADXL_MAX_X, ADXL_MAX_X, 0, 0);
    input_set_abs_params(input, ABS_Y, - ADXL_MAX_Y, ADXL_MAX_Y, 0, 0);
    input_set_abs_params(input, ABS_Z, - ADXL_MAX_Z, ADXL_MAX_Z, 0, 0);

    input_set_drvdata(input, adxl);

    error =  devm_request_threaded_irq(&client->dev, client->irq,
                      NULL, adxl345_irq, IRQF_ONESHOT,
                      "adxl345", adxl);
    if (error) {
        dev_err(&client->dev,
            "Failed to enable IRQ, error: %d\n", error);
        return error;
    }

    /* Disable the IRQ, we'll enable it in ar1021_i2c_open() */
    disable_irq(client->irq);

    error = input_register_device(adxl->input);
    if (error) {
        dev_err(&client->dev,
            "Failed to register input device, error: %d\n", error);
        return error;
    }

    i2c_set_clientdata(client, adxl);
    return 0;

    write8(client, ADXL_BW, 0x0A);
    write8(client, ADXL_INT_ENABLE, 0x00);
    write8(client, ADXL_FIFO_MODE, 0x00);
    write8(client, ADXL_DATA_FORMAT, 0x00);
    write8(client, ADXL_POWER_CTL, 0x08);
    return 0;
}

static int adxl345_remove(struct i2c_client *client)
{
    struct adxl345 *adxl = i2c_get_clientdata(client);
    free_irq(client->irq, adxl);
    input_unregister_device(adxl->input);
    devm_kfree(&client->dev, adxl);
    write8(client, ADXL_POWER_CTL, 0x00);
    return 0;
}

static struct i2c_device_id adxl345_idtable[] = {
    { "adxl345", 0 },
    { }
};

MODULE_DEVICE_TABLE(i2c, adxl345_idtable);

#ifdef CONFIG_OF
static const struct of_device_id adxl345_of_match[] = {
    { .compatible = "accelerometer,adxl345", },
    {}
};

MODULE_DEVICE_TABLE(of, adxl345_of_match);
#endif

static struct i2c_driver adxl345_driver = {
    .driver = {
        .name   = "adxl345",
        .of_match_table = of_match_ptr(adxl345_of_match),
    },
    .id_table       = adxl345_idtable,
    .probe          = adxl345_probe,
    .remove         = adxl345_remove,
};

module_i2c_driver(adxl345_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Accelerometer ADXL345 I2C Driver");
MODULE_AUTHOR("Tycho Tatitscheff <tycho.tatitscheff@ensam.eu>");
