#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/semaphore.h>

/* Defines necesarios para el modulo */
#define MAX_BUF_ELEMS 8
#define MAX_CHARS_AUX_BUF MAX_BUF_ELEMS * 2
/* Cabeceras de funciones usadas */
int __init__ prodcons_init(void);
void __exit__ prodcons_exit(void);
int prodcons_open (struct inode *, struct file *);
int prodcons_release (struct inode *, struct file *);
ssize_t prodcons_read (struct file *, char __user *, size_t, loff_t *);
ssize_t prodcons_write (struct file *, const char __user *, size_t, loff_t *);

/* Estructuras necesarias*/
static struct file_operations fops = {
    .read = prodcons_read,
    .open = prodcons_open,
    .release = prodcons_release,
   	.write = prodcons_write
};
static struct kfifo cbuf;
/* semaforo para controlar la escritura */
DEFINE_SEMAPHORE(mtx);
/* semaforo para controlar la escritura */
DEFINE_SEMAPHORE(huecos);
/* semaforo para controlar la lectura */
DEFINE_SEMAPHORE(elementos);

int prodcons_open (struct inode *node, struct file *filp) {
    try_module_get(THIS_MODULE);
    return 0;
}

void prodcons_release(struct inode *node, struct file *filp) {
    module_put(THIS_MODULE);
}


ssize_t prodcons_read (struct file *filp, char __user *buf, size_t len, loff_t *off) {
    int value;
    int nbytes;
    char auxbuf[MAX_CHARS_AUX_BUF+1];

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

    ssize_t size sprintf(auxbuf, "%i\n", val);

    if (copy_to_user(buf, auxbuf, size)) {
        return -ENOMEM;
    }

    up(&huecos);
    up (&mtx);

    return size;
}

ssize_t prodcons_write (struct file *filp, const char __user *buf, size_t len, loff_t *off) {
    char auxbuf[MAX_CHARS_AUX_BUF + 1];
    int val = 0;

    if (copy_from_user(auxbuf, buf, len)) {
        return -ENOMEM;
    }
    auxbuf[len] = '\0';
    sscanf(auxbuf, "%d", &val);

    if (down_interruptible(&huecos)) {
        return -EINTR;
    }

    if (down_interruptible(&mtx)) {
        up(&huecos);
        return -EINTR;
    }

    kfifo_in(cbuf, &val, sizeof(int));
    
    up(&elementos);
    up(&mtx);

    return len;
}



int __init__ prodcons_init(void) {
	if (kfifo_alloc(&cbuf, MAX_BUF_ELEMS*sizeof(int), GFP_KERNEL)) {
        return -ENOMEM;
	}

    sema_init(&mtx, 1);
    sema_init(&huecos, 0);
    sema_init(&elementos, MAX_BUF_ELEMS);

    return 0;
}

void __exit__ prodcons_exit(void) {

}


module_init(prodcons_init);
module_exit(prodcons_exit);