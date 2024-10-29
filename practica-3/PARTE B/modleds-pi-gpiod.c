#include <linux/module.h>
#include <asm-generic/errno.h>
#include <linux/gpio.h>
#include <linux/usb.h>

#define DEVICE_NAME "leds"

#define ALL_LEDS_ON 0x7
#define ALL_LEDS_OFF 0
#define NR_GPIO_LEDS  3

MODULE_DESCRIPTION("Modulo de manejo de LEDS de la placa Bee con un driver - FDI-UCM");
MODULE_AUTHOR("Enrique Rios Rios");
MODULE_AUTHOR("Alejandro Orgaz");
MODULE_LICENSE("GPL");

/* Parametros globales */
static char is_file_open = 0;

static struct class* class
static struct cdev* modleds-pidev

/* File operations struct */
static struct file_operations fops = {
  .read = modleds_read
  .write = modleds_write
  .open = modleds_open
  .release = modleds_release 
}

/* Register the driver with the shortcut struct miscdevice */
static struct miscdevice misc_modleds {
  .minor = MISC_DYNAMIC_MINOR,
  .name = DEVICE_NAME,
  .mode = 0666,
  .fops = &fops
};

/* Array to hold gpio descriptors */
struct gpio_desc* gpio_descriptors[NR_GPIO_LEDS];

/* Actual GPIOs used for controlling LEDs */
const int led_gpio[NR_GPIO_LEDS] = {25, 27, 4};

/* Set led state to that specified by mask */
static inline int set_pi_leds(unsigned int mask) {
  int i;
  for (i = 0; i < NR_GPIO_LEDS; i++)
    gpiod_set_value(gpio_descriptors[i], (mask >> i) & 0x1 );
  return 0;
}

static int __init modleds_init(void)
{
  // Initialize driver
  int ret;
  ret = misc_register(&misc_chardev)

  if (ret) {
    pr_err("Couldn`t register modleds, sorry!\n");
    return ret;
  }

  printk(KERN_INFO "El registro del dispositivo se ha realizado con éxito\n");

  return 0;

  // codigo original
  int i, j;
  int err = 0;
  char gpio_str[10];

  for (i = 0; i < NR_GPIO_LEDS; i++) {
    /* Build string ID */
    sprintf(gpio_str, "led_%d", i);
    /* Request GPIO */
    if ((err = gpio_request(led_gpio[i], gpio_str))) {
      pr_err("Failed GPIO[%d] %d request\n", i, led_gpio[i]);
      goto err_handle;
    }

    /* Transforming into descriptor */
    if (!(gpio_descriptors[i] = gpio_to_desc(led_gpio[i]))) {
      pr_err("GPIO[%d] %d is not valid\n", i, led_gpio[i]);
      err = -EINVAL;
      goto err_handle;
    }

    gpiod_direction_output(gpio_descriptors[i], 0);
  }



  return 0;
err_handle:
  for (j = 0; j < i; j++)
    gpiod_put(gpio_descriptors[j]);
  return err;
}

static void __exit modleds_exit(void) {
  int i;

  printk(KERN_INFO "El modulo ha sido desactivado satisfactoriamente\n");
  set_pi_leds(ALL_LEDS_OFF);

  for (i = 0; i < NR_GPIO_LEDS; i++)
    gpiod_put(gpio_descriptors[i]);
}

module_init(modleds_init);
module_exit(modleds_exit);

static int device_open(struct inode *inode, struct file *file) {
  if (is_file_open) {
    return -EBUSY;
  }

  // Marco la variable como marcada
  is_file_open = 1;


}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Modleds");
