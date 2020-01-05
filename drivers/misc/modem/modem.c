#define DEBUG
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/earlysuspend.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <mach/irqs.h>
#include <linux/kernel.h>
#include <linux/semaphore.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/syscalls.h>
#include <linux/unistd.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/timer.h>

#include <linux/delay.h>
#include <linux/syscalls.h>
#include <linux/platform_device.h>
 
#include <linux/wakelock.h>

#include <mach/../../gpio-names.h>
#include <linux/switch.h>

struct modem_simcard_data {
    int card_present;
    struct switch_dev	sdev;
};
static struct modem_simcard_data *_simcard_data = NULL;

//SIM card detection ++
#define MODEM_SIM_CD_GPIO_IRQ        TEGRA_GPIO_PV7
#define MODEM_SIM_CD_GPIO_IRQ_NAME   "modem_sim_cd"
//SIM card detection --

int modem_PW = 0, sim_state = 0;
static struct platform_device *modem;
static struct wake_lock modem_wake_lock;
static int wake_lock_state = 0;

#define USB_PHY_RESET_GPIO           TEGRA_GPIO_PC7    //23
#define USB_PHY_RESET_GPIO_NAME      "usb_phy_reset"

#define MODEM_PWR_N_EN_GPIO          TEGRA_GPIO_PW0    //176
#define MODEM_PWR_N_EN_GPIO_NAME     "modem_pow_N_en"

//#define MODEM_RESET_GPIO           TEGRA_GPIO_PH7    //63
#define MODEM_RESET_GPIO             TEGRA_GPIO_PB1    //9   UART4_CTS
#define MODEM_RESET_GPIO_NAME        "modem_reset"

#define MODEM_W_DISABLE_GPIO         TEGRA_GPIO_PDD7   //239
#define MODEM_W_DISABLE_GPIO_NAME    "modem_w_disable"

#define EN_3V3_MODEM_GPIO            TEGRA_GPIO_PP0    //120
#define EN_3V3_MODEM_GPIO_NAME       "EN_3V3_MODEM"

//TEGRA_GPIO_PP3    123

void modem_hsdpa_pwr (int on);
static int modem_suspend(struct platform_device  *pdev, pm_message_t state);
static int modem_resume(struct platform_device  *pdev);

static ssize_t show_modem_power_status(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t store_modem_power_status(struct device *dev, struct device_attribute *attr, char *buf, size_t count);
static ssize_t show_sim_status(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t store_wake_lock_status(struct device *dev, struct device_attribute *attr, char *buf, size_t count);
static ssize_t show_wake_lock_status(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t show_other_wake_lock_status(struct device *dev, struct device_attribute *attr, char *buf);

MODULE_LICENSE("GPL");

#define MODEM_MAJOR     232
#define MODEM_DRIVER	"Modem_driver"

static struct device_attribute modem_power_attr =
    __ATTR(ModemPower, 0660, show_modem_power_status, store_modem_power_status);

static struct device_attribute sim_attr =
    __ATTR(SIM, 0660, show_sim_status, NULL);

static struct device_attribute wakelock_attr =
    __ATTR(Wakelock, 0660, show_wake_lock_status, store_wake_lock_status);

static struct device_attribute other_wakelock_attr =
    __ATTR(OtherWakelock, 0660, show_other_wake_lock_status, NULL);

static struct attribute *modem_attrs[] = {
    &modem_power_attr.attr,
    &sim_attr.attr,
    &wakelock_attr.attr,
    &other_wakelock_attr.attr,
    NULL,
};

static struct attribute_group modem_attr_grp = {
    .name = "control",
    .attrs = modem_attrs,
};

static struct file_operations modem_file_operations = 
{
    .owner = THIS_MODULE,
};

void modem_hsdpa_pwr (int on)
{
    printk("3G Modem %s\n", on ? "on" : "off");
    if(!on)
        gpio_direction_output(EN_3V3_MODEM_GPIO, 0);
    else
    	 gpio_direction_output(EN_3V3_MODEM_GPIO, 1);

    modem_PW=on;
}

static ssize_t print_switch_name(struct switch_dev *sdev, char *buf)
{
    return sprintf(buf, "%s\n", "sim_cd");
}

static ssize_t print_switch_state(struct switch_dev *sdev, char *buf)
{
    struct modem_simcard_data *simcard_data = container_of(sdev, struct modem_simcard_data , sdev);
    return sprintf(buf, "%s\n", (simcard_data->card_present ? "online" : "offline"));
}

static irqreturn_t sim_carddetect_irq(int irq, void *data)
{
    struct modem_simcard_data *simcard_data = data;
    int value;

    value = gpio_get_value(TEGRA_GPIO_PV7);
    sim_state = value;
    if (value != simcard_data->card_present) {
        simcard_data->card_present = value;
        switch_set_state(&simcard_data->sdev, simcard_data->card_present);
        printk("sim_carddetect : %d\n", simcard_data->card_present);
    }
    msleep(200);
    return IRQ_HANDLED;
};

static int modem_probe(struct platform_device *pdev)
{
    int ret=-1, rc;
    struct modem_simcard_data *simcard_data;

//SIM card detection ++
    simcard_data  = kzalloc(sizeof(struct modem_simcard_data), GFP_KERNEL);
    if (!simcard_data)
        return -ENOMEM;

    _simcard_data = simcard_data; 

    if (gpio_is_valid(MODEM_SIM_CD_GPIO_IRQ)) {
        rc = gpio_request(MODEM_SIM_CD_GPIO_IRQ, MODEM_SIM_CD_GPIO_IRQ_NAME);
        if (rc) {
            printk("failed to request sim_cd gpio\n");
            goto error_gpio_request;
        }
        tegra_gpio_enable(MODEM_SIM_CD_GPIO_IRQ);
        gpio_direction_input(MODEM_SIM_CD_GPIO_IRQ);

        simcard_data->card_present = gpio_get_value(MODEM_SIM_CD_GPIO_IRQ);
        rc = request_threaded_irq(gpio_to_irq(MODEM_SIM_CD_GPIO_IRQ), NULL,
             sim_carddetect_irq,
             IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
             MODEM_SIM_CD_GPIO_IRQ_NAME, simcard_data);
        if (rc)	{
            printk("MODEM_SIM_CD_GPIO_IRQ request irq error\n");
            goto error_gpio_request;
        }
        simcard_data->sdev.name = "sim_cd";
        simcard_data->sdev.print_name = print_switch_name;
        simcard_data->sdev.print_state = print_switch_state;
        simcard_data->sdev.state = gpio_get_value(MODEM_SIM_CD_GPIO_IRQ);
        rc = switch_dev_register(&simcard_data->sdev);
		if (rc < 0) {
            printk("Error registering sim_cd switch!\n");
            goto error_gpio_request;
        }
        switch_set_state(&simcard_data->sdev, simcard_data->card_present);
    }
error_gpio_request:
//SIM card detection --

    printk(" >>> modem_probe  \n");
    tegra_gpio_enable(MODEM_PWR_N_EN_GPIO); //3G_POWER_ON  //Simon
    tegra_gpio_enable(MODEM_RESET_GPIO); //3G_RESE_ON  //Simon
    tegra_gpio_enable(MODEM_W_DISABLE_GPIO); //3G_W_DISABLE  //Simon
    tegra_gpio_enable(EN_3V3_MODEM_GPIO); //EN_3V3_MODEM_GPIO  //Simon
    
    ret = gpio_request(MODEM_PWR_N_EN_GPIO, MODEM_PWR_N_EN_GPIO_NAME);
        
    if (ret < 0)
        pr_err("%s: gpio_request failed for gpio %d\n",			__func__, MODEM_PWR_N_EN_GPIO);
    else {
        gpio_direction_output(MODEM_PWR_N_EN_GPIO, 0);
        printk("Set MODEM_PWR_EN_GPIO [%d] as 0 \n",MODEM_PWR_N_EN_GPIO);
    }

    ret = gpio_request(MODEM_RESET_GPIO, MODEM_RESET_GPIO_NAME);
    if (ret < 0)
        pr_err("%s: gpio_request failed for gpio %d\n",			__func__, MODEM_RESET_GPIO);
    else {
        gpio_direction_output(MODEM_RESET_GPIO, 0);
        printk("Set MODEM_RESET_GPIO [%d] as 0 \n",MODEM_RESET_GPIO);
    }

    ret = gpio_request(MODEM_W_DISABLE_GPIO, MODEM_W_DISABLE_GPIO_NAME);
    if (ret < 0)
        pr_err("%s: gpio_request failed for gpio %d\n",			__func__, MODEM_W_DISABLE_GPIO);
    else {
        gpio_direction_output(MODEM_W_DISABLE_GPIO, 0);
        printk("Set MODEM_W_DISABLE_GPIO [%d] as 0 \n",MODEM_W_DISABLE_GPIO);
    }

    ret = gpio_request(EN_3V3_MODEM_GPIO, EN_3V3_MODEM_GPIO_NAME);
    if (ret < 0)
        pr_err("%s: gpio_request failed for gpio %d\n",			__func__, EN_3V3_MODEM_GPIO);
    else {
        gpio_direction_output(EN_3V3_MODEM_GPIO, 1);
        printk("Set EN_3V3_MODEM_GPIO [%d] as 1 \n",EN_3V3_MODEM_GPIO);
    }

    ret = gpio_request(TEGRA_GPIO_PP3, "AP_WAKE_MODEM");
    if (ret < 0)
        pr_err("%s: gpio_request failed for gpio %d\n",			__func__, TEGRA_GPIO_PP3);
    else {
        gpio_direction_output(TEGRA_GPIO_PP3, 1);
        printk("Set AP-Wake-Modem [%d] as 1 \n",TEGRA_GPIO_PP3);
    }

    sim_state = simcard_data->card_present;
    modem_hsdpa_pwr(0);
    return 0;
}

#ifdef CONFIG_PM
static int modem_suspend(struct platform_device  *pdev, pm_message_t state)
{
    printk(">>> modem_suspend\n");
    return 0;
}

static int modem_resume(struct platform_device  *pdev)
{
    printk(">>> modem_resume\n");
    _simcard_data->card_present = gpio_get_value(MODEM_SIM_CD_GPIO_IRQ);
    switch_set_state(&_simcard_data->sdev, _simcard_data->card_present);

    return 0;
}
#endif

static const char on_string[] = "on";
static const char off_string[] = "off";
static const char online_string[] = "online";
static const char offline_string[] = "offline";

static ssize_t show_modem_power_status(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%s\n", modem_PW ? on_string : off_string);
}

static ssize_t store_modem_power_status(struct device *dev, struct device_attribute *attr, char *buf, size_t count)
{
    if(!memcmp(buf, on_string, 2)){
        if(!modem_PW)
            modem_hsdpa_pwr(1);
        return sprintf(buf, "%s\n", on_string);
    }else if(!memcmp(buf, off_string, 3)){
        if(modem_PW)
            modem_hsdpa_pwr(0);
        return sprintf(buf, "%s\n", off_string);
    }
    return sprintf(buf, "unknow\n");
}

static ssize_t show_sim_status(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%s\n", sim_state ? online_string : offline_string);
}

static ssize_t show_wake_lock_status(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%s\n", wake_lock_state ? on_string : off_string);
}

static ssize_t store_wake_lock_status(struct device *dev, struct device_attribute *attr, char *buf, size_t count)
{
    if(!memcmp(buf, on_string, 2)){
        if(!wake_lock_state){
            wake_lock_state = 1;
            wake_lock(&modem_wake_lock);
        }
        return sprintf(buf, "%s\n", on_string);
    }else if(!memcmp(buf, off_string, 3)){
        if(wake_lock_state){
            wake_lock_state = 0;
            wake_unlock(&modem_wake_lock);
        }
        return sprintf(buf, "%s\n", off_string);
    }
    return sprintf(buf, "unknow\n");
}

static ssize_t show_other_wake_lock_status(struct device *dev, struct device_attribute *attr, char *buf)
{
    int ret = has_wake_lock_besides_me(&modem_wake_lock);
    return sprintf(buf, "%s\n", ret ? "locked" : "unlock");
}

static struct platform_driver modem_driver = {
	.probe		= modem_probe,
#ifdef CONFIG_PM
	.suspend	= modem_suspend,
	.resume		= modem_resume,
#endif
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= MODEM_DRIVER
	},
};

static int modem_init(void)
{
	int retval;

	printk(">>> modem_init\n");

	retval = platform_driver_register(&modem_driver);
	if (retval < 0)
	  return retval;

    modem = platform_device_register_simple(MODEM_DRIVER, -1, NULL, 0);	  
    if (IS_ERR(modem))
        return -1;

    sysfs_create_group(&modem->dev.kobj, &modem_attr_grp);
	retval = register_chrdev(MODEM_MAJOR, MODEM_DRIVER, &modem_file_operations);
	if (retval < 0)
	{
		printk(KERN_WARNING "Can't get major %d\n", MODEM_MAJOR);
		return retval;
	}

    wake_lock_init(&modem_wake_lock, WAKE_LOCK_SUSPEND, "Modem_driver");
	printk("<<< modem_init\n");
	return 0;
}

static void modem_exit(void)
{
    printk("Modem_exit...");
    sysfs_remove_group(&modem->dev.kobj, &modem_attr_grp);
    unregister_chrdev(MODEM_MAJOR, MODEM_DRIVER);
}

module_init(modem_init);
module_exit(modem_exit);

