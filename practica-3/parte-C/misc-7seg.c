#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <asm-generic/errno.h>
#include <linux/gpio.h>
#include <linux/delay.h>

MODULE_DESCRIPTION("Misc Display7s Kernel Module - FDI-UCM");
MODULE_AUTHOR("Juan Carlos Saez");
MODULE_LICENSE("GPL");

/* Bits associated with each segment */
#define DS_A 0x80
#define DS_B 0x40
#define DS_C 0x20
#define DS_D 0x10
#define DS_E 0x08
#define DS_F 0x04
#define DS_G 0x02
#define DS_DP 0x01
#define SEGMENT_COUNT 8

/* Indices of GPIOs used by this module */
enum {
	SDI_IDX = 0,
	RCLK_IDX,
	SRCLK_IDX,
	NR_GPIO_DISPLAY
};

/* Pin numbers */
const int display_gpio[NR_GPIO_DISPLAY] = { 18, 23, 24 };

/* Array to hold GPIO descriptors */
struct gpio_desc* gpio_descriptors[NR_GPIO_DISPLAY];

const char* display_gpio_str[NR_GPIO_DISPLAY] = { "sdi", "rclk", "srclk" };

/* Codificación de cada carácter hexadecimal para el display de 7 segmentos */
const unsigned char hex_encoding[] = {
	DS_A | DS_B | DS_C | DS_D | DS_E | DS_F,       // 0
	DS_B | DS_C,                                   // 1
	DS_A | DS_B | DS_G | DS_E | DS_D,              // 2
	DS_A | DS_B | DS_C | DS_D | DS_G,              // 3
	DS_F | DS_G | DS_B | DS_C,                     // 4
	DS_A | DS_F | DS_G | DS_C | DS_D,              // 5
	DS_A | DS_F | DS_G | DS_C | DS_D | DS_E,       // 6
	DS_A | DS_B | DS_C,                            // 7
	DS_A | DS_B | DS_C | DS_D | DS_E | DS_F | DS_G,// 8
	DS_A | DS_B | DS_C | DS_F | DS_G,              // 9
	DS_A | DS_B | DS_C | DS_E | DS_F | DS_G,       // A
	DS_C | DS_D | DS_E | DS_F | DS_G,              // B
	DS_A | DS_D | DS_E | DS_F,                     // C
	DS_B | DS_C | DS_D | DS_E | DS_G,              // D
	DS_A | DS_D | DS_E | DS_F | DS_G,              // E
	DS_A | DS_E | DS_F | DS_G                      // F
};

#define DEVICE_NAME "display7s" /* Device name */

/*
 *  Prototypes
 */
static ssize_t display7s_write(struct file*, const char*, size_t, loff_t*);

/* Simple initialization of file_operations interface with a single operation */
static struct file_operations fops = {
	.write = display7s_write,
};

static struct miscdevice display7s_misc = {
	.minor = MISC_DYNAMIC_MINOR, /* kernel dynamically assigns a free minor# */
	.name = DEVICE_NAME,         /* kernel auto-creates device file */
	.mode = 0666,                /* dev node permissions */
	.fops = &fops,               /* connect to this driver's functionality */
};

/* Update the 7-segment display with the configuration specified by the data parameter */
static void update_7sdisplay(unsigned char data)
{
	int i = 0;
	int value = 0;

	for (i = 0; i < SEGMENT_COUNT; i++)
	{
		/* Explore current bit (from most significant to least significant) */
		if (0x80 & (data << i))
			value = 1;
		else
			value = 0;

		/* Set value of serial input */
		gpiod_set_value(gpio_descriptors[SDI_IDX], value);
		/* Generate clock cycle in shift register */
		gpiod_set_value(gpio_descriptors[SRCLK_IDX], 1);
		msleep(1);
		gpiod_set_value(gpio_descriptors[SRCLK_IDX], 0);
	}

	/* Generate clock cycle in output register to update 7-seg display */
	gpiod_set_value(gpio_descriptors[RCLK_IDX], 1);
	msleep(1);
	gpiod_set_value(gpio_descriptors[RCLK_IDX], 0);
}

/*
 * Called when a process writes to dev file: echo "hi" > /dev/display7s
 */
static ssize_t display7s_write(struct file* file, const char __user* buf, size_t len, loff_t* offset) {
	char user_input;
	unsigned char display_value;

	// Validación del tamaño de entrada (asegura que sea un solo carácter)
	if (len != 2) {
		return -EINVAL; // Argumento inválido
	}

	// Leer un solo carácter del espacio de usuario
	if (copy_from_user(&user_input, buf, 1)) {
		return -EFAULT; // Error en la copia desde el espacio de usuario
	}

	// Validar que el carácter esté en el rango hexadecimal (0-9, A-F)
	if ((user_input >= '0' && user_input <= '9') || (user_input >= 'A' && user_input <= 'F')) {
		int index = (user_input <= '9') ? user_input - '0' : user_input - 'A' + 10;
		display_value = hex_encoding[index];
	}
	else {
		return -EINVAL; // Argumento inválido
	}

	// Actualizar el display con el valor correspondiente
	update_7sdisplay(display_value);

	return len; // Devolver el número de bytes escritos
}

static int __init display7s_misc_init(void)
{
	int i, j;
	int err = 0;
	struct device* device;

	for (i = 0; i < NR_GPIO_DISPLAY; i++)
	{

		/* Request the GPIO */
		if ((err = gpio_request(display_gpio[i], display_gpio_str[i])))
		{
			pr_err("Failed GPIO[%d] %d request\n", i, display_gpio[i]);
			goto err_handle;
		}

		/* Transform number into descriptor */
		if (!(gpio_descriptors[i] = gpio_to_desc(display_gpio[i])))
		{
			pr_err("GPIO[%d] %d is not valid\n", i, display_gpio[i]);
			err = -EINVAL;
			goto err_handle;
		}

		/* Configure as an output pin */
		gpiod_direction_output(gpio_descriptors[i], 0);
	}

	/* Set everything as LOW */
	for (i = 0; i < NR_GPIO_DISPLAY; i++)
		gpiod_set_value(gpio_descriptors[i], 0);

	/* Register misc device that exposes 7-segment display */
	err = misc_register(&display7s_misc);

	if (err)
	{
		pr_err("Couldn't register misc device\n");
		goto err_handle;
	}

	device = display7s_misc.this_device;

	dev_info(device, "Display7s driver registered successfully.\n");

	return 0;
err_handle:
	for (j = 0; j < i; j++)
		gpiod_put(gpio_descriptors[j]);
	return err;
}

static void __exit display7s_misc_exit(void)
{
	int i = 0;

	/* Unregister character device */
	misc_deregister(&display7s_misc);

	/* Clear display */
	update_7sdisplay(0);

	/* Free up pins */
	for (i = 0; i < NR_GPIO_DISPLAY; i++)
		gpiod_put(gpio_descriptors[i]);

	pr_info("Display7s driver deregistered. Bye\n");
}

module_init(display7s_misc_init);
module_exit(display7s_misc_exit);
