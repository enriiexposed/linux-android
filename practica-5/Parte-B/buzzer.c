#include <linux/init.h>
#include <linux/kernel.h> 
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/miscdevice.h>
#include <linux/pwm.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/spinlock.h>
#include <linux/irqreturn.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>


MODULE_AUTHOR("Enrique Ríos Ríos");
MODULE_AUTHOR("Alejandro Orgaz");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("A module that reproduces songs with raspberry's buzzer");

/* Parámetros del módulo cargable */
static int beat = 120;
module_param(beat, int, 0444);
MODULE_PARM_DESC(beat, "Marca el numero de figuras negras que sonaran en un minuto");

#define DEVICE_NAME "buzzer"

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
static struct work_struct work;

/* Variables globales para el timer del kernel */
static struct timer_list timer;

/* Cabeceras de funciones*/
static int buzzer_open(struct inode *inode, struct file *file);
static int buzzer_release(struct inode *inode, struct file *file);
static ssize_t buzzer_read(struct file *filp, char *buf, size_t len, loff_t *off);
static ssize_t buzzer_write(struct file *filp, const char __user *buf, size_t len, loff_t *off);

static struct file_operations fops = {
    .read = buzzer_read,
    .write = buzzer_write
};

static struct miscdevice misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = DEVICE_NAME,
    .mode = 0666,
    .fops = &fops
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
        printk(KERN_INFO "The button wont have no effect when buzzer_request equals buzzer_none or buzzer_config\n");
        break;
    }

  return IRQ_HANDLED;
}


#define MAX_BEAT_LENGTH 20

static ssize_t buzzer_write(struct file *filp, const char __user *buf, size_t len, loff_t *off) {
    char *buzzer;
    char *command, *params;
    unsigned long flags;
    int result = 0;

    if (len >= PAGE_SIZE) {
        return -EINVAL;
    }

    buzzer = kmalloc(len + 1, GFP_KERNEL);
    if (!buzzer)
        return -ENOMEM;

    if (copy_from_user(buzzer, buf, len)) {
        kfree(buzzer);
        return -EFAULT;
    }
    
    buzzer[len] = '\0';

    command = strsep(&buzzer, " ");
    params = buzzer;

    spin_lock_irqsave(&lock, flags);

    if (buzzer_state == BUZZER_PLAYING) {
        spin_unlock_irqrestore(&lock, flags);
        kfree(buzzer);
        return -EBUSY;
    }

    buzzer_request = REQUEST_CONFIG;

    if (strcmp(command, "music") == 0) {
        char *note_data;
        int i = 0;

        if (!params) {
            result = -EINVAL;
            goto cleanup;
        }

        if (song) {
            vfree(song);
            song = NULL;
        }

        song = vmalloc(PAGE_SIZE);
        if (!song) {
            result = -ENOMEM;
            goto cleanup;
        }

        while ((note_data = strsep(&params, ",")) != NULL) {
            char *freq_str = strsep(&note_data, ":");
            char *len_str = note_data;

            if (!freq_str || !len_str || i >= PAGE_SIZE / sizeof(struct music_step)) {
                result = -EINVAL;
                goto cleanup;
            }

            unsigned int temp_freq, temp_len;

            if (kstrtouint(freq_str, 10, &temp_freq) ||
                kstrtouint(len_str, 10, &temp_len)) {
                result = -EINVAL;
                goto cleanup;
            }

            song[i].freq = temp_freq;
            song[i].len = temp_len;

            i++;
        }

        printk(KERN_INFO "Melodía configurada con %d notas.\n", i);
    } else if (strcmp(command, "beat") == 0) {
        int new_beat;

        if (!params || kstrtouint(params, 10, &new_beat) || new_beat == 0) {
            result = -EINVAL;
            goto cleanup;
        }

        beat = new_beat;
        printk(KERN_INFO "Beat configurado a %u.\n", beat);
    } else {
        result = -EINVAL;
        goto cleanup;
    }

    spin_unlock_irqrestore(&lock, flags);

    return 0;

cleanup:
    spin_unlock_irqrestore(&lock, flags);
    kfree(buzzer);

    if (result != 0 && song) {
        vfree(song);
        song = NULL;
    }

    return (result == 0) ? len : result;
}


static ssize_t buzzer_read(struct file *filp, char *buf, size_t len, loff_t *off) {
    int nr_bytes;
    char aux_mesg[MAX_BEAT_LENGTH];

    if ((*off) > 0) {
        return 0;
    }
    
    nr_bytes = sprintf(aux_mesg, "beat=%d\n", beat);

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
    int err;

    misc_register(&misc);
    
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
    gpiod_direction_input(desc_button);
    pr_info("Request del boton completada\n");

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
    pr_info("Interrupciones configuradas para el SW1\n");

    INIT_WORK(&work, run_buzzer_state);
    pr_info("Tarea diferida iniciada correctamente\n");
    
    /* Init del pwm asociado al buzzer */
    pwm_device = pwm_request(0, PWM_DEVICE_NAME);
    if (IS_ERR(pwm_device)) {
        err = PTR_ERR(pwm_device);
        goto err_handle;
    }
    pr_info("PWM asociado al buzzer iniciado correctamente\n");

    timer_setup(&timer, wait_for_next_note, 0);  
    timer.expires=jiffies+msecs_to_jiffies((60*1000)/beat);
    pr_info("Timer configurado correctamente\n");

    if ((song = vmalloc(PAGE_SIZE)) == NULL) {
        err = -ENOMEM;
        goto pwm_err;
    }
    pr_info("Memoria dinámica asociada a la cancion reservada correctamente\n");
    return 0;

pwm_err:
    pwm_free(pwm_device);
    del_timer_sync(&timer);
err_handle:
    if (gpio_out_ok) {
        gpiod_put(desc_button);
    }
    
    return err;    
}


static void __exit buzzer_exit(void) {
    flush_scheduled_work();
    vfree(song);
    pwm_free(pwm_device);
    del_timer_sync(&timer);
    free_irq(gpio_button_irqn, NULL);
    gpiod_put(desc_button);
    misc_deregister(&misc);
    pr_info("Todos los recursos han sido liberados"); 
}



module_init(buzzer_init);
module_exit(buzzer_exit);
