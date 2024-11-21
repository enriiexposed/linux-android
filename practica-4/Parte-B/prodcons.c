#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/kfifo.h>
#include <linux/miscdevice.h>
#include <linux/semaphore.h>


MODULE_DESCRIPTION("Prodcons Kernel Module - LIN FDI-UCM");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Enrique Rios Rios");
MODULE_AUTHOR("Alejandro Orgaz");

/* Defines necesarios para el modulo */
#define MAX_BUF_ELEMS 8
#define MAX_CHARS_AUX_BUF MAX_BUF_ELEMS * 2
#define DEVICE_NAME "prodcons"  /* Dev name as it appears in /proc/devices   */
/* Cabeceras de funciones usadas */
int prodcons_init(void);
void prodcons_exit(void);
static int prodcons_open (struct inode *node, struct file *filp);
static int prodcons_release (struct inode *node, struct file *filp);
static ssize_t prodcons_read (struct file *filp, char __user *buf, size_t len, loff_t *off);
static ssize_t prodcons_write (struct file *filp, const char __user *buf, size_t len, loff_t *off);

/* Estructuras necesarias*/
static struct file_operations fops = {
    .read = prodcons_read,
    .open = prodcons_open,
    .release = prodcons_release,
   	.write = prodcons_write
};

static struct miscdevice misc_prodcons = {
    .minor = MISC_DYNAMIC_MINOR,    /* kernel dynamically assigns a free minor# */
    .name = DEVICE_NAME, /* when misc_register() is invoked, the kernel
                        * will auto-create device file as /dev/chardev ;
                        * also populated within /sys/class/misc/ and /sys/devices/virtual/misc/ */
    .mode = 0666,     /* ... dev node perms set as specified here */
    .fops = &fops,    /* connect to this driver's 'functionality' */
};

static struct kfifo cbuf;

static struct semaphore mtx;
static struct semaphore elementos;
static struct semaphore huecos;


static int prodcons_open (struct inode *node, struct file *filp) {
    try_module_get(THIS_MODULE);
    return 0;
}

static int prodcons_release(struct inode *node, struct file *filp) {
    module_put(THIS_MODULE);
    return 0;
}


static ssize_t prodcons_read (struct file *filp, char __user *buf, size_t len, loff_t *off) {
    int val;
    char auxbuf[MAX_CHARS_AUX_BUF+1];
    ssize_t size;

    if ((*off) > 0) {
        return 0;
    }

    /* No permito que otro proceso o hilo pueda leer elementos */
    if (down_interruptible(&elementos)) {
        return -EINTR;
    }

    if (down_interruptible(&mtx)) {
        /* Permito leer elementos de nuevo */
        up(&elementos);
        return -EINTR;
    }
    
    kfifo_out(&cbuf, &val, sizeof(int));

    size = sprintf(auxbuf, "%i\n", val);

    if (copy_to_user(buf, auxbuf, size)) {
        return -ENOMEM;
    }

    up (&mtx);
    up(&huecos);

    (*off) += len;

    return size;
}

static ssize_t prodcons_write (struct file *filp, const char __user *buf, size_t len, loff_t *off) {
    char auxbuf[MAX_CHARS_AUX_BUF + 1];
    int val = 0;

    if (copy_from_user(auxbuf, buf, len)) {
        return -ENOMEM;
    }
    
    auxbuf[len] = '\0';
    if (sscanf(auxbuf, "%d%*s", &val) != 1) {
        printk(KERN_INFO "Argumento incorrecto al escribir\n");
        return -EINVAL;
    }

    printk(KERN_INFO "Numero a insertar: %d", val);

    // no dejamos que otro proceso escriba en los huecos
    if (down_interruptible(&huecos)) {
        return -EINTR;
    }

    if (down_interruptible(&mtx)) {
        up(&huecos);
        return -EINTR;
    }

    kfifo_in(&cbuf, &val, sizeof(int));
    
    up(&mtx);
    up(&elementos);

    (*off) += len;

    return len;
}



int prodcons_init(void) {
	if (kfifo_alloc(&cbuf, MAX_BUF_ELEMS*sizeof(int), GFP_KERNEL)) {
        return -ENOMEM;
	}

    sema_init(&mtx, 1);
    sema_init(&huecos, MAX_BUF_ELEMS);
    sema_init(&elementos, 0);

    misc_register(&misc_prodcons);

    printk(KERN_INFO "Modulo cargado correctamente\n");

    return 0;
}

void prodcons_exit(void) {
    misc_deregister(&misc_prodcons);
    kfifo_free(&cbuf);
    printk(KERN_INFO "Modulo descargado correctamente\n");
}


module_init(prodcons_init);
module_exit(prodcons_exit);

