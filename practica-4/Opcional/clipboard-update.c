#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/semaphore.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,2,1)
#define __cconst__ const
#else
#define __cconst__ 
#endif

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Clipboard-update Kernel Module with Semaphores - FDI-UCM");
MODULE_AUTHOR("Juan Carlos Saez");

#define BUFFER_LENGTH       PAGE_SIZE
#define DEVICE_NAME "clipboard_update" /* Dev name as it appears in /proc/devices   */
#define CLASS_NAME "clip"

/*
 * Global variables are declared as static, so are global within the file.
 */

static dev_t start;
static struct cdev* chardev = NULL;
static struct class* class = NULL;
static struct device* device = NULL;
static char *clipboard;  // Space for the "clipboard"

/* Semaphores */
static struct semaphore sem_lock;    // Semaphore as a mutex (lock)
static struct semaphore sem_waiting; // Semaphore to block readers (initially 0)

/* Counter for processes waiting on the sem_waiting semaphore */
static int waiting_readers = 0;

static ssize_t clipboard_write(struct file *filp, const char __user *buf, size_t len, loff_t *off) {
    int available_space = BUFFER_LENGTH - 1;

    if ((*off) > 0) /* The application can write in this entry just once !! */
        return 0;

    if (len > available_space) {
        printk(KERN_INFO "clipboard: not enough space!!\n");
        return -ENOSPC;
    }

    /* Lock critical section */
    if (down_interruptible(&sem_lock))
        return -ERESTARTSYS;

    /* Transfer data from user to kernel space */
    if (copy_from_user(&clipboard[0], buf, len)) {
        up(&sem_lock); // Release the lock in case of error
        return -EFAULT;
    }

    clipboard[len] = '\0'; /* Add the `\0' */
    *off += len;           /* Update the file position indicator */

    /* Wake up all waiting readers */
    while (waiting_readers > 0) {
        up(&sem_waiting);  // Increment semaphore to unblock readers
        waiting_readers--; // Decrease the counter of waiting readers
    }

    printk(KERN_INFO "clipboard: clipboard updated.\n");

    /* Release the lock */
    up(&sem_lock);

    return len;
}

static ssize_t clipboard_read(struct file *filp, char __user *buf, size_t len, loff_t *off) {
    int nr_bytes;

    if ((*off) > 0) /* Tell the application that there is nothing left to read */
        return 0;

    /* Increment the number of waiting readers */
    waiting_readers++;

    /* Wait until clipboard is updated */
    if (down_interruptible(&sem_waiting)) {
        waiting_readers--; // Decrease counter if interrupted
        return -ERESTARTSYS;
    }

    /* Lock critical section */
    if (down_interruptible(&sem_lock)) {
        return -ERESTARTSYS;
    }

    nr_bytes = strlen(clipboard);

    if (len < nr_bytes) {
        up(&sem_lock); // Release the lock
        return -ENOSPC;
    }

    /* Transfer data from the kernel to userspace */
    if (copy_to_user(buf, clipboard, nr_bytes)) {
        up(&sem_lock); // Release the lock
        return -EINVAL;
    }

    (*off) += nr_bytes; /* Update the file pointer */

    /* Release the lock */
    up(&sem_lock);

    return nr_bytes;
}

static struct file_operations fops = {
    .read = clipboard_read,
    .write = clipboard_write,
};

static char *custom_devnode(__cconst__ struct device *dev, umode_t *mode) {
    if (!mode)
        return NULL;
    if (MAJOR(dev->devt) == MAJOR(start))
        *mode = 0666;
    return NULL;
}

int init_clipboard_module(void) {
    int major;    /* Major number assigned to our device driver */
    int minor;    /* Minor number assigned to the associated character device */
    int ret;

    clipboard = (char *)vmalloc(BUFFER_LENGTH);

    if (!clipboard) {
        printk(KERN_INFO "Can't allocate clipboard memory");
        return -ENOMEM;
    }

    memset(clipboard, 0, BUFFER_LENGTH);

    /* Initialize semaphores */
    sema_init(&sem_lock, 1);     // Binary semaphore (mutex)
    sema_init(&sem_waiting, 0);  // Semaphore initialized to 0

    /* Get available (major, minor) range */
    if ((ret = alloc_chrdev_region(&start, 0, 1, DEVICE_NAME))) {
        printk(KERN_INFO "Can't allocate chrdev_region()");
        goto error_alloc_region;
    }

    /* Create associated cdev */
    if ((chardev = cdev_alloc()) == NULL) {
        printk(KERN_INFO "cdev_alloc() failed ");
        ret = -ENOMEM;
        goto error_alloc;
    }

    cdev_init(chardev, &fops);

    if ((ret = cdev_add(chardev, start, 1))) {
        printk(KERN_INFO "cdev_add() failed ");
        goto error_add;
    }

    /* Create custom class */
    class = class_create(THIS_MODULE, CLASS_NAME);

    if (IS_ERR(class)) {
        pr_err("class_create() failed \n");
        ret = PTR_ERR(class);
        goto error_class;
    }

    /* Establish function that will take care of setting up permissions for device file */
    class->devnode = custom_devnode;

    /* Creating device */
    device = device_create(class, NULL, start, NULL, DEVICE_NAME);

    if (IS_ERR(device)) {
        pr_err("Device_create failed\n");
        ret = PTR_ERR(device);
        goto error_device;
    }

    major = MAJOR(start);
    minor = MINOR(start);

    printk(KERN_INFO "I was assigned major number %d. To talk to\n", major);
    printk(KERN_INFO "the driver try to cat and echo to /dev/%s.\n", DEVICE_NAME);
    printk(KERN_INFO "Remove the module when done.\n");

    printk(KERN_INFO "Clipboard-update: Module loaded.\n");

    return 0;

error_device:
    class_destroy(class);
error_class:
    /* Destroy chardev */
    if (chardev) {
        cdev_del(chardev);
        chardev = NULL;
    }
error_add:
    /* Destroy partially initialized chardev */
    if (chardev)
        kobject_put(&chardev->kobj);
error_alloc:
    unregister_chrdev_region(start, 1);
error_alloc_region:
    vfree(clipboard);

    return ret;
}

void exit_clipboard_module(void) {
    if (device)
        device_unregister(device);

    if (class)
        class_destroy(class);

    /* Destroy chardev */
    if (chardev)
        cdev_del(chardev);

    /*
     * Release major minor pair
     */
    unregister_chrdev_region(start, 1);

    vfree(clipboard);

    printk(KERN_INFO "Clipboard-update: Module unloaded.\n");
}

module_init(init_clipboard_module);
module_exit(exit_clipboard_module);

