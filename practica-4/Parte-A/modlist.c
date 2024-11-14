#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include <linux/printk.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SMP-safe Kernel Module para insertar números en una lista sin memoria dinámica");
MODULE_AUTHOR("Enrique Ríos Ríos y Alejandro Orgaz Fernández");

#define MAX_SIZE 4096
#define LIST_POOL_SIZE 100  // Tamaño del pool estático de elementos de lista

static struct proc_dir_entry *proc_entry;
static LIST_HEAD(numlist);
static spinlock_t list_lock;
static atomic_t usage_count = ATOMIC_INIT(0);

struct list_item {
    int data;
    struct list_head links;
    bool in_use;  // Indica si el elemento está en uso o disponible
};

static struct list_item list_pool[LIST_POOL_SIZE];  // Pool estático de elementos de lista

// Inicialización del pool estático
static void init_list_pool(void) {
    int i;
    for (i = 0; i < LIST_POOL_SIZE; i++) {
        list_pool[i].in_use = false;
        INIT_LIST_HEAD(&list_pool[i].links);
    }
}

// Obtiene un elemento libre del pool
static struct list_item* get_free_list_item(void) {
    int i;
    for (i = 0; i < LIST_POOL_SIZE; i++) {
        if (!list_pool[i].in_use) {
            list_pool[i].in_use = true;
            return &list_pool[i];
        }
    }
    return NULL;  // No hay elementos disponibles
}

// Libera un elemento y lo devuelve al pool
static void release_list_item(struct list_item *item) {
    item->in_use = false;
}

// Función de lectura de la lista enlazada
static ssize_t read_numlist(struct file *filp, char __user *buf, size_t len, loff_t *off) {
    ssize_t bytes_written = 0;
    struct list_head *i;
    struct list_item *item;
    char bufaux[MAX_SIZE] = "";
    char *ptrbufaux = bufaux;

    if ((*off) > 0) return 0;

    spin_lock(&list_lock);

    list_for_each(i, &numlist) {
        item = list_entry(i, struct list_item, links);
        int num_bytes = snprintf(ptrbufaux, MAX_SIZE - (ptrbufaux - bufaux), "%d\n", item->data);
        
        if (num_bytes < 0 || ptrbufaux - bufaux + num_bytes > MAX_SIZE) {
            spin_unlock(&list_lock);
            return -ENOSPC;
        }
        
        ptrbufaux += num_bytes;
    }

    *ptrbufaux = '\0';
    bytes_written = ptrbufaux - bufaux;

    spin_unlock(&list_lock);

    if (len < bytes_written) return -ENOMEM;
    if (copy_to_user(buf, bufaux, bytes_written)) return -EFAULT;

    *off += bytes_written;
    return bytes_written;
}

// Función de escritura de la lista enlazada
static ssize_t write_numlist(struct file *filp, const char __user *buf, size_t len, loff_t *off) {
    struct list_head *pos, *q;
    struct list_item *item;
    int n;
    char bufaux[MAX_SIZE];

    if (*off > 0) return 0;
    if (len > MAX_SIZE - 1) return -EINVAL;
    if (copy_from_user(bufaux, buf, len)) return -EFAULT;

    bufaux[len] = '\0';

    spin_lock(&list_lock);

    if (sscanf(bufaux, "add %i", &n) == 1) {
        struct list_item *new_item = get_free_list_item();
        if (!new_item) {
            spin_unlock(&list_lock);
            return -ENOMEM;
        }
        new_item->data = n;
        list_add_tail(&new_item->links, &numlist);
    } 
    else if (sscanf(bufaux, "remove %i", &n) == 1) {
        list_for_each_safe(pos, q, &numlist) {
            item = list_entry(pos, struct list_item, links);
            if (item->data == n) {
                list_del(pos);
                release_list_item(item);
                break;
            }
        }
    } 
    else if (strcmp(bufaux, "cleanup\n") == 0) {
        list_for_each_safe(pos, q, &numlist) {
            item = list_entry(pos, struct list_item, links);
            list_del(pos);
            release_list_item(item);
        }
    } 
    else {
        spin_unlock(&list_lock);
        return -EINVAL;
    }

    spin_unlock(&list_lock);

    *off += len;
    return len;
}

struct proc_ops numlist_ops = {
    .proc_read = read_numlist,
    .proc_write = write_numlist,
};

static int numlist_open(struct inode *inode, struct file *file) {
    atomic_inc(&usage_count);
    return 0;
}

static int numlist_release(struct inode *inode, struct file *file) {
    atomic_dec(&usage_count);
    return 0;
}

int modlist_init(void) {
    spin_lock_init(&list_lock);
    init_list_pool();

    proc_entry = proc_create("modlist", 0666, NULL, &numlist_ops);
    if (!proc_entry) {
        printk(KERN_INFO "No se pudo crear la entrada en /proc\n");
        return -ENOMEM;
    }

    printk(KERN_INFO "Modulo cargado correctamente.\n");
    return 0;
}

void modlist_clean(void) {
    struct list_head *pos, *q;
    struct list_item *item;

    if (atomic_read(&usage_count) == 0) {
        spin_lock(&list_lock);

        // Limpieza de todos los elementos en la lista numlist
        list_for_each_safe(pos, q, &numlist) {
            item = list_entry(pos, struct list_item, links);
            list_del(pos);
            release_list_item(item);
        }

        spin_unlock(&list_lock);

        // Eliminación de la entrada /proc
        remove_proc_entry("modlist", NULL);
        printk(KERN_INFO "Modulo descargado y memoria liberada correctamente.\n");
    } else {
        printk(KERN_INFO "El módulo está en uso y no se puede descargar.\n");
    }
}


module_init(modlist_init);
module_exit(modlist_clean);

