#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/pwm.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/timer.h>


MODULE_NAME("Buzzer Module")
MODULE_AUTHOR("Enrique Ríos Ríos")
MODULE_AUTHOR("Alejandro Orgaz")
MODULE_LICENSE("GPL")
MODULE_DESCRIPTION("A module that reproduces songs with raspberry's buzzer")

#define DEVICE_NAME "buzzer"

/* Parámetros del módulo cargable */
static unsigned short beat = 120;
module_param(unsigned short, beat, 0);
MODULE_PARAM_DESC(beat, "Marca el numero de figuras negras que sonaran en un minuto");

struct music_step
{
	unsigned int freq : 24; /* Frequency in centihertz */
	unsigned int len : 8;	/* Duration of the note */
};
/* Duda ¿Memoria dinamica o array de tamaño predefinido? */
static struct music_step* song;

static struct music_step* next_note=NULL; /* Puntero a la siguiente nota de la melodía 
											actual  (solo alterado por tarea diferida) */

typedef enum {
    BUZZER_STOPPED, /* Buzzer no reproduce nada (la melodía terminó o no ha comenzado) */
    BUZZER_PAUSED,	/* Reproducción pausada por el usuario */
    BUZZER_PLAYING	/* Buzzer reproduce actualmente la melodía */
} buzzer_state_t;

static buzzer_state_t buzzer_state=BUZZER_STOPPED; /* Estado actual de la reproducción */

typedef enum {
    REQUEST_START,		/* Usuario pulsó SW1 durante estado BUZZER_STOPPED */
    REQUEST_RESUME,		/* Usuario pulsó SW1 durante estado BUZZER_PAUSED */
    REQUEST_PAUSE,		/* Usuario pulsó SW1 durante estado BUZZER_PLAYING */
    REQUEST_CONFIG,		/* Usuario está configurando actualmente una nueva melodía vía /dev/buzzer  */
    REQUEST_NONE			/* Indicador de petición ya gestionada (a establecer por tarea diferida) */
} buzzer_request_t;

static buzzer_request_t buzzer_request=REQUEST_NONE;

/* Cerrojo para proteger actualización/consulta 
de variables buzzer_state y buzzer_request */
static spinlock_t lock; 

/* Dispositivo que genera las ondas musicales */
#define PWM_DEVICE_NAME "pwmchip0"

struct pwm_device *pwm_device = NULL;
struct pwm_state pwm_state;

/* Variables globales para la interrupcion del boton*/
#define GPIO_BUTTON 22
struct gpio_desc* desc_button = NULL;
static int gpio_button_irqn = -1;

/* Variables globales para la workqueue */
static struct work_struct;

/* Variables globales para el timer del kernel */
struct timer_list timer;

/* Cabeceras de funciones*/
static int buzzer_open(struct inode *inode, struct file *file);
static int buzzer_release(struct inode *inode, struct file *file);
static ssize_t buzzer_read(struct file *filp, char *buf, size_t len, loff_t *off);
static ssize_t buzzer_write(struct file *filp, const char __user *buf, size_t len, loff_t *off);

static struct file_oeprations fops = {
    .read = buzzer_read
    .write = buzzer_write
    .open = buzzer_open
    .release = buzzer_release
}

static struct miscdevice misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = DEVICE_NAME,
    .mode = 0666,
    .fops = &fops,
};

/* Funcion que se ejecutará cuando haya que reproducir la nota correspondiente */
void wait_for_next_note(struct timer_list *timer) {
    
}

/* Tarea diferida que activará el buzzer dependiendo del estado*/
static void run_buzzer_state(struct work_struct *work) {
    switch(buzzer_request) {
        case REQUEST_START:
            pwm_init_state(pwm_device, &pwm_state);
            pwm_set_relative_duty_cycle(&pwm_state, 70, 100);
			pwm_state.enabled = true;
            pwm_apply_state(pwm_device, &pwm_state);
        case REQUEST_NONE:
            break;
        case REQUEST_PAUSE:
            pwm_disable(pwm_device);
            break;
        case REQUEST_RESUME:
            pwm_init_state(pwm_device, &pwm_state);
            break;
        case REQUEST_CONFIG:
            pwm_disable(pwm_device);
            break;
    }
}

/* Interrupt handler for button **/
static irqreturn_t gpio_irq_handler(int irq, void *dev_id)
{
#ifdef MANUAL_DEBOUNCE
  static unsigned long last_interrupt = 0;
  unsigned long diff = jiffies - last_interrupt;
  if (diff < 20)
    return IRQ_HANDLED;

  last_interrupt = jiffies;
#endif
    
  switch(buzzer_state) {
    case BUZZER_STOPPED:
        buzzer_request = REQUEST_START;
        break;
    case BUZZER_PAUSED:
        buzzer_request = REQUEST_RESUME;
        break;
    case BUZZER_PLAYING:
        if (schedule_work(&work)) {
            
        }
        buzzer_request = REQUEST_PAUSE;
        break;
    default:
        printk(KERN_INFO "The button wont have no effect 
        when buzzer_request equals 'buzzer_none' or 'buzzer_config'\n");
        break;
    }

  return IRQ_HANDLED;
}


#define Max_BEAT_LENGTH = 20;

static ssize_t buzzer_read(struct file *filp, char *buf, size_t len, loff_t *off) {
    int nr_bytes;
    char aux_mesg[Max_BEAT_LENGTH];

    if ((*off) > 0) {
        return 0;
    }
    
    nr_bytes = sprintf(aux_mesg, "beat=%d", beat);

    if (len < nr_bytes) {
        return -ENOSPC;
    }

     if (copy_to_user(buf, aux_mesg, nr_bytes)) {
        return -EINVAL;
    }
    (*off) += nr_bytes; /* Update the file pointer */

    return nr_bytes;
}

static int __init buzzer_init(void) {
    char gpio_out_ok = 0;
    misc_register(&misc);
    /* Requesting Button's GPIO */
    if ((err = gpio_request(GPIO_BUTTON, "button"))) {
        pr_err("ERROR: GPIO %d request\n", GPIO_BUTTON);
        goto err_handle;
    }

    gpio_out_ok = 1;

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

    INIT_WORK(&work);

    pwm_request = pwm_request(0, PWM_DEVICE_NAME);
    if (ERR_PTR(pwm_request)){
        goto err_handle;
    }

    timer_setup(&my_timer,my_timer_function, 0);  
    my_timer.expires=jiffies+msecs_to_jiffies(beat/(60*1000));

    try_module_get(THIS_MODULE);

    if ((song = vmalloc(PAGE_SIZE)) == NULL) {
        goto pwm_err;
    }

    return 0;
    
pwm_err:
    pwm_free(pwm_request);
err_handle:
    if (gpio_out_ok) {
        gpiod_put(desc_button);
    }
    
    return err;    
}


static void __exit buzzer_exit(void) {
    misc_deregister(&misc);
    vfree(song);
    flush_scheduled_work(&work);
    free_irq(gpio_button_irqn, NULL);
    gpiod_put(desc_button);
    module_put(THIS_MODULE);
}



module_init(buzzer_init);
module_exit(buzzer_exit);