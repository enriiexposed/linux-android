#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/miscdevice.h>

MODULE_NAME("Buzzer Module")
MODULE_AUTHOR("Enrique Ríos Ríos")
MODULE_AUTHOR("Alejandro Orgaz")
MODULE_LICENSE("GPL")
MODULE_DESCRIPTION("A module that reproduces songs with raspberry's buzzer")

#define DEVICE_NAME "buzzer"

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

#define Max_BEAT_LENGTH = 20;

static ssize_t buzzer_read(struct file *filp, char *buf, size_t len, loff_t *off) {
    int nr_bytes;
    char aux_mesg[Max_BEAT_LENGTH];

    if ((*off) > 0) 
        retur
    
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
    misc_register(&misc);
}


static void __exit buzzer_exit(void) {
    misc_deregister(&misc);
}



module_init(buzzer_init);
module_exit(buzzer_exit);