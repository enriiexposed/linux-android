#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Timerleds module");
MODULE_AUTHOR("Enrique Rios Rios");
MODULE_AUTHOR("Alejandro Fernandez Orgaz");

/* Definición de parámetros variables del módulo */
static short timer_period_ms = 200;
module_param(timer_period_ms, short, 0444);
MODULE_PARM_DESC(timer_period_ms, "A timer in ms (default = 0)");

/* Definicion de informacion importante sobre los pines de GPIOD que usaremos */
#define NR_GPIO_LEDS  3
const int led_gpio[NR_GPIO_LEDS] = {25, 27, 4};
/* Array to hold gpio descriptors */
struct gpio_desc* gpio_descriptors[NR_GPIO_LEDS];
#define GPIO_BUTTON 22
struct gpio_desc* desc_button = NULL;
static int gpio_button_irqn = -1;

static struct timer_list time;

static char counter_enable = 1;
static char leds_to_show = 0;

#define mask_converted(mask) (((mask & 4) >> 2) | (mask & 2) | ((mask & 1) << 2))
#define count_up(mask) ((mask + 1) % 8)

static void set_pi_leds(struct timer_list *timer) {
  int i;
  if (counter_enable) {
    char leds_out = mask_converted(leds_to_show);
    for (i = 0; i < NR_GPIO_LEDS; i++) {
      gpiod_set_value(gpio_descriptors[i], (leds_out >> i) & 0x1 ); 
	}
	leds_to_show = count_up(leds_to_show);
  }

  mod_timer(&time, jiffies + msecs_to_jiffies(timer_period_ms));
}

static irqreturn_t gpio_irq_handler(int irq, void *dev_id) {
  static unsigned long last_interrupt = 0;
  unsigned long diff = jiffies - last_interrupt;
  if (diff < 20)
    return IRQ_HANDLED;

  // conmutamos el counter para que no siga mostrandose (no se si es asi)
  counter_enable ^= 1; 

  last_interrupt = jiffies;
  return IRQ_HANDLED;
}

/* Funciones de init y clean del modulo */
int init_timer_module( void ) {
  /* Timer que alternará los leds de la placa dado un tiempo pasado por parámetro al módulo */
  timer_setup(&time, set_pi_leds, 0);
  time.expires = jiffies + msecs_to_jiffies(timer_period_ms);
  add_timer(&time);
	/* Conseguimos acceso a los leds de la placa Raspberry Pi*/
	int i;
  int err;
  unsigned char gpio_out_ok = 0;
  for (i = 0; i < NR_GPIO_LEDS; i++) {
    /* Build string ID */
    char gpio_str[10];
    sprintf(gpio_str, "led_%d", i);
    //Requesting the GPIO
    if ((err = gpio_request(led_gpio[i], gpio_str))) {
      pr_err("Failed GPIO[%d] %d request\n", i, led_gpio[i]);
      goto err_handle;
    }

    /* Transforming into descriptor **/
    if (!(gpio_descriptors[i] = gpio_to_desc(led_gpio[i]))) {
      pr_err("GPIO[%d] %d is not valid\n", i, led_gpio[i]);
      err = -EINVAL;
      goto err_handle;
    }

    gpiod_direction_output(gpio_descriptors[i], 0);
  }

  /* Conseguimos el acceso a los switches sw1 */

  /* Requesting Button's GPIO */
  if ((err = gpio_request(GPIO_BUTTON, "button"))) {
    pr_err("ERROR: GPIO %d request\n", GPIO_BUTTON);
    goto err_handle;
  }

  /* Configure Button */
  if (!(desc_button = gpio_to_desc(GPIO_BUTTON))) {
    pr_err("GPIO %d is not valid\n", GPIO_BUTTON);
    err = -EINVAL;
    goto err_handle;
  }

  gpio_out_ok = 1;
  //configure the BUTTON GPIO as input
  gpiod_direction_input(desc_button);

  //Get the IRQ number for our GPIO
  gpio_button_irqn = gpiod_to_irq(desc_button);
  pr_info("IRQ Number = %d\n", gpio_button_irqn);

  if (request_irq(gpio_button_irqn,             //IRQ number
                  gpio_irq_handler,   //IRQ handler
                  IRQF_TRIGGER_RISING,        //Handler will be called in raising edge
                  "button_leds",               //used to identify the device name using this IRQ
                  NULL)) {                    //device id for shared IRQ
    pr_err("my_device: cannot register IRQ ");
    goto err_handle;
  }

  return 0;

int j;
err_handle:
  for (j = 0; j < i; j++)
    gpiod_put(gpio_descriptors[j]);

  if (gpio_out_ok)
    gpiod_put(desc_button);

  return err;
}

void cleanup_timer_module(void) {
	int i = 0;

  del_timer_sync(&time);

  free_irq(gpio_button_irqn, NULL);
  
  for (i = 0; i < NR_GPIO_LEDS; i++)
    gpiod_put(gpio_descriptors[i]);

  gpiod_put(desc_button);
}


module_init( init_timer_module );
module_exit( cleanup_timer_module );

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Timerleds module");
MODULE_AUTHOR("Enrique Rios Rios");
MODULE_AUTHOR("Alejandro Fernandez Orgaz");
