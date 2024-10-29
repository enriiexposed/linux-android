#include <linux/module.h>
#include <asm-generic/errno.h>
#include <linux/gpio.h>
#include <linux/usb.h>

#define SUCCESS 0
#define BUFAUX_MAX_SIZE 2
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

/* El numero de GPIO de los leds que modificaremos para encenderlos o apagarlos */
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
  ret = misc_register(&misc_modleds)

  if (ret) {
    pr_err("Couldn`t register modleds, sorry!\n");
    return ret;
  }

  printk(KERN_INFO "El registro del dispositivo se ha realizado con éxito\n");

  return 0;

  // Solicitamos el dominio de los pines de la practica
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

// Manejamos errores si los hubiera
err_handle:
  for (j = 0; j < i; j++)
    gpiod_put(gpio_descriptors[j]);
  return err;
}

static void __exit modleds_exit(void) {
  
  misc_deregister(&misc_modleds);
  
  int i;

  printk(KERN_INFO "El modulo ha sido desactivado satisfactoriamente\n");
  set_pi_leds(ALL_LEDS_OFF);

  for (i = 0; i < NR_GPIO_LEDS; i++)
    gpiod_put(gpio_descriptors[i]);
}

module_init(modleds_init);
module_exit(modleds_exit);

/*
Definicion de las operaciones de ficheros (que hará el driver cada vez que abra, cierra, lea o escriba sobre el archivo)
*/
static int device_open(struct inode *inode, struct file *file) {
  if (is_file_open) {
    return -EBUSY;
  }
  
  struct device* device;
  device = misc_chardev.this_device;

  if (!device) {
    return -ENODEV;
  }

  is_file_open = 1;

  try_module_get(THIS_MODULE);

  return SUCCESS;
}

static int device_release(struct inode *inode, struct file* file) {
  is_file_open = 0;

  module_put(THIS_MODULE);
}

static ssize_t device_read(struct file *filp,   /* see include/linux/fs.h   */
                           char *buf,    /* buffer to fill with data */
                           size_t len,   /* length of the buffer     */
                           loff_t * off) {
  printk(KERN_ALERT "La operacion de leer no está soportada");
  return -EPERM
}


/* La escritura sobre este fichero deberia de ser un nuemro de 0 al 7 */

char bufaux[BUFAUX_MAX_SIZE]
static ssize_t
device_write(struct file filp*, const char __user *buf, size_t len, loff_t* off) {
  /* Si estamos pasando mas bytes de los que debemos,
   entonces el numero es un argumento incorrecto 
   (tiene que estar entre 0 y 7, es decir, como mucho un byte) 
   
   Ademas, como nuestro buffer es de solo 2 bytes, 
   tambien nos quedaremos sin espacio
   */
  if (len > BUFAUX_MAX_SIZE) {
    return -ENOSPC
  }

  if (copy_from_user(&bufaux[0], bufaux, len)) {
    return -EFAULT;
  }
  
  clipboard[len] = '\0';
  
  int leds;

  /* El argumento es incorrecto (no es un numero) */
  if ((leds = atoi(bufaux)) == 0) {
    return -EINVAL;
  }

  if (leds < 0 || leds > 7) {
    return -EINVAL;
  }

  set_pi_leds(leds);

  (*off) += len
  return len;
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Modleds");
