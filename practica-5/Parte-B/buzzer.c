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

MODULE_LICENSE("GPL");

/* Debug Mode */

// #define debug_mode

#define MANUAL_DEBOUNCE


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
static struct music_step* song = NULL;

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

/* variable del candado spin-lock */
static struct spinlock spin;

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

/* Transform frequency in centiHZ into period in nanoseconds */
static inline unsigned int freq_to_period_ns(unsigned int frequency)
{
	if (frequency == 0)
		return 0;
	else
		return DIV_ROUND_CLOSEST_ULL(100000000000UL, frequency);
}

/* Check if the current step is and end marker */
static inline int is_end_marker(struct music_step *step)
{
	return (step->freq == 0 && step->len == 0);
}

static inline int calculate_delay_ms(unsigned int note_len, unsigned int qnote_ref)
{
	unsigned char duration = (note_len & 0x7f);
	unsigned char triplet = (note_len & 0x80);
	unsigned char i = 0;
	unsigned char current_duration;
	int total = 0;

	/* Calculate the total duration of the note
	 * as the summation of the figures that make
	 * up this note (bits 0-6)
	 */
	while (duration) {
		current_duration = (duration) & (1 << i);

		if (current_duration) {
			/* Scale note accordingly */
			if (triplet)
				current_duration = (current_duration * 3) / 2;
			/*
			 * 24000/qnote_ref denote number of ms associated
			 * with a whole note (redonda)
			 */
			total += (240000) / (qnote_ref * current_duration);
			/* Clear bit */
			duration &= ~(1 << i);
		}
		i++;
	}
	return total;
}

/* Tarea diferida que activará el buzzer dependiendo del estado*/
static void run_buzzer_state(struct work_struct *work) {
    unsigned long flags;
    /* Cambiamos los estados en base al request que nos llegue*/
    spin_lock_irqsave(&spin, flags);
    switch(buzzer_request) {
        case REQUEST_START:
        if (song != NULL) {
            buzzer_state = BUZZER_PLAYING;
            timer.expires = jiffies + msecs_to_jiffies(calculate_delay_ms(next_note->len, beat));
            add_timer(&timer);
        }
            break;
        case REQUEST_RESUME:
        buzzer_state = BUZZER_PLAYING;
        /* Modifica la config del pwm para que vuelva a sonar */
        timer.expires = jiffies + msecs_to_jiffies(calculate_delay_ms(next_note->len, beat));
        add_timer(&timer);
            break;
        case REQUEST_PAUSE:
        buzzer_state = BUZZER_PAUSED;
            break;
        case REQUEST_CONFIG:
        buzzer_state = BUZZER_STOPPED;
            break;
        case REQUEST_NONE:
            if (buzzer_state == BUZZER_PLAYING) {
                if (is_end_marker(next_note)) {
                    /* reiniciamos la canción */
                    next_note = song;
                } else {
                    timer.expires = jiffies + msecs_to_jiffies(calculate_delay_ms(next_note->len, beat));
                    add_timer(&timer);
                }
            }
            break;
        default:
            pr_info("El valor de buzzer_request esta corrompido\n");
            break;
    }
    spin_unlock_irqrestore(&spin, flags);

    switch(buzzer_request) {
        case REQUEST_START:
            /* Config del timer */
            if (song != NULL) {
                /* Config del pwm para que suene */
                #ifndef debug_mode
                pwm_init_state(pwm_device, &pwm_state);
                pwm_state.period = freq_to_period_ns(next_note->freq);
                pwm_disable(pwm_device);
                if (pwm_state.period > 0) {
                    pwm_set_relative_duty_cycle(&pwm_state, 70, 100);
                    pwm_state.enabled = true;
                    pwm_apply_state(pwm_device, &pwm_state);
                }
                #else
                printk("Configurado el buzzer para la nota que va a sonar\n");
                #endif
            } else {
                pr_info("No se puede iniciar, no hay una cancion configurada");
            } 
            break;
        case REQUEST_RESUME:
            #ifndef debug_mode
            pwm_state.period = freq_to_period_ns(next_note->freq);
            pwm_disable(pwm_device);
            if (pwm_state.period > 0) {
                pwm_set_relative_duty_cycle(&pwm_state, 70, 100);
                pwm_state.enabled = true;
                pwm_apply_state(pwm_device, &pwm_state);
            }
            #else 
            printk("La cancion se va a reproducir\n");
            #endif
            break;
        case REQUEST_PAUSE:
            #ifndef debug_mode
            pwm_disable(pwm_device);
            #else 
            printk("El buzzer se ha parado, la cancion ha sido pausada\n");
            #endif
            break;
        case REQUEST_CONFIG:
            #ifndef debug_mode
            pwm_disable(pwm_device);
            #else 
            printk("Se esta introduciendo una canción\n");
            #endif
            break;
        case REQUEST_NONE:
        // si no ha cambiado el request en la interrupcion del botón, importante aquí
            if (buzzer_state == BUZZER_PLAYING) {
                if (is_end_marker(next_note)) {
                    buzzer_state = BUZZER_STOPPED;
                    #ifndef debug_mode
                    pwm_disable(pwm_device);
                    #else
                    printk("La cancion ha acabado\n");
                    #endif
                } else {
                    #ifndef debug_mode
                    pwm_state.period = freq_to_period_ns(next_note->freq);
                    pwm_disable(pwm_device);
                    if (pwm_state.period > 0) {
                        pwm_set_relative_duty_cycle(&pwm_state, 60, 100);
                        pwm_state.enabled = true;
                        pwm_apply_state(pwm_device, &pwm_state);
                    }
                    #else
                    printk("La cancion sigue reproduciendose\n");
                    #endif
                }
            }          
            break;
        default:
            pr_info("El valor de buzzer_request esta corrompido\n");
            break;
    }

    spin_lock_irqsave(&spin, flags);
    buzzer_request = REQUEST_NONE;    
    spin_unlock_irqrestore(&spin, flags);
    pr_info("Fin de la tarea diferida\n");      
}


/* Funcion que se ejecutará cuando haya que reproducir la nota correspondiente */
static void wait_for_next_note(struct timer_list *timer) {
    unsigned long flags;
    if (next_note != NULL) {
        /* Paso a la siguiente nota*/
        printk(KERN_INFO "Nota Inicial: %d --- ", next_note->freq);
        next_note++;
        printk(KERN_INFO "Nota Siguiente: %d\n", next_note->freq);
        /* Mando de nuevo la tarea diferida */
        schedule_work(&work);
    }
}

/* Interrupcion del boton */
static irqreturn_t gpio_irq_handler(int irq, void *dev_id)
{
#ifdef MANUAL_DEBOUNCE
  static unsigned long last_interrupt = 0;
  unsigned long diff = jiffies - last_interrupt;
  if (diff < 20)
    return IRQ_HANDLED;

  last_interrupt = jiffies;
#endif
    unsigned long flags;
    printk("Spinlock no adquirido\n");

    spin_lock_irqsave(&spin, flags);

    printk("Spinlock adquirido por interrupcion\n");

  switch(buzzer_state) {
    case BUZZER_STOPPED:
        buzzer_request = REQUEST_START;
        printk(KERN_INFO "Solicitud para iniciar el reproductor\n");
        break;
    case BUZZER_PAUSED:
        del_timer_sync(&timer);
        printk(KERN_INFO "Has pulsado el boton. El buzzer continúa su reproduccion\n");
        buzzer_request = REQUEST_RESUME;
        break;
    case BUZZER_PLAYING:
        del_timer_sync(&timer);
        printk(KERN_INFO "Has pulsado el boton. El buzzer detiene su reproduccion\n");
        buzzer_request = REQUEST_PAUSE;
        break;
    }
    spin_unlock_irqrestore(&spin, flags);
    /* Inicio el trabajo diferido */
    printk("Fuera del candado, boton pulsado, ejecuto la tarea diferida\n");
    schedule_work(&work);
  return IRQ_HANDLED;
}



#define MAX_BEAT_LENGTH 20

static ssize_t buzzer_write(struct file *filp, const char __user *buf, size_t len, loff_t *off) {
    char *buzzer;
    char params[PAGE_SIZE];
    int new_beat;
    unsigned long flags;
    int result = 0;

    if (len >= PAGE_SIZE) {
        return -EINVAL;
    }

    buzzer = kmalloc(len + 1, GFP_KERNEL);
    if (!buzzer) {
        return -ENOMEM;
    }

    if (copy_from_user(buzzer, buf, len)) {
        result = EFAULT;
        goto mem_err;
    }
    buzzer[len] = '\0';

    pr_info("Puntero usuario movido a una variable del kernel\n");

    spin_lock_irqsave(&lock, flags);

    pr_info("Candado cogido\n");

    if (buzzer_state != BUZZER_STOPPED) {
        result = EBUSY;
        goto buzzer_playing_err;
    }
    
    buzzer_request = REQUEST_CONFIG;

    spin_unlock_irqrestore(&lock, flags);

    if (sscanf(buzzer, "music %s", params) == 1) {
        char *note_data;
        char *notes = params;
        int i = 0;

        printk(KERN_INFO "Cancion: %s\n", buzzer);

        // Borra memoria si ya habia
        if (song) {
            vfree(song);
        }

        // Guarda la memoria de la nueva cancion
        song = vmalloc(PAGE_SIZE);
        if (!song) {
            result = ENOMEM;
            goto cleanup;
        }

        note_data = strsep(&notes, ",");
        while (note_data != NULL) {
            i++;
            printk("%s\n", note_data);
            int temp_freq, temp_len;
            if (sscanf(note_data, "%d:%x", &temp_freq, &temp_len) != 2) {
                result = EINVAL;
                goto cleanup;
            }
            song[i].freq = temp_freq;
            song[i].len = temp_len;
    
            note_data = strsep(&notes, ",");
        }
        
        song[i].freq = 0;
        song[i].len = 0;

        next_note = song;
        
        printk(KERN_INFO "Melodía configurada con %d notas.\n", i);
    } else if (sscanf(buzzer, "beat %d", &new_beat) == 1) {
        if (new_beat <= 0) {
            printk("Error al insertar el nuevo beat\n");
            result = EINVAL;
            goto mem_err;
        }
        beat = new_beat;
        printk(KERN_INFO "Beat configurado a %u.\n", beat);
    } else {
        result = EINVAL;
        goto mem_err;
    }

    schedule_work(&work);

    kfree(buzzer);

    *off += len;

    return len;

cleanup:
    vfree(song);
buzzer_playing_err:
    spin_unlock_irqrestore(&spin, flags);
mem_err:
    kfree(buzzer);
    return -result;
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

static int buzzer_open(struct inode *inode, struct file *file) {
    try_module_get(THIS_MODULE);
    return 0;
}
 
static int buzzer_release(struct inode *inode, struct file *file) {
    module_put(THIS_MODULE);
    return 0;
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
    pr_info("Interrupciones configuradas para el SW1\n");

    /* Init de la tarea diferida */
    INIT_WORK(&work, run_buzzer_state);
    pr_info("Tarea diferida iniciada correctamente\n");
    
    /* Init del pwm asociado al buzzer */
    pwm_device = pwm_request(0, PWM_DEVICE_NAME);
    if (IS_ERR(pwm_device)) {
        err = PTR_ERR(pwm_device);
        goto err_handle;
    }
    
    pr_info("PWM asociado al buzzer iniciado correctamente\n");

    /* Init del spinlock */
    spin_lock_init(&spin);

    /* Init del timer */
    timer_setup(&timer, wait_for_next_note, 0);  
    pr_info("Timer configurado correctamente\n");
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
    pr_info("Todos los recursos han sido liberados\n"); 
}


module_init(buzzer_init);
module_exit(buzzer_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("A module that reproduces songs with raspberry's buzzer");