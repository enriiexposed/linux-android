#include <linux/module.h> /* Requerido por todos los módulos */
#include <linux/kernel.h> /* Definición de KERN_INFO */
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include <linux/printk.h>

MODULE_LICENSE("GPL"); /* Licencia del módulo */
MODULE_DESCRIPTION("Módulo del Kernel para insertar numeros en una lista");
MODULE_AUTHOR("Enrique Ríos Ríos");
MODULE_AUTHOR("Alejandro Orgaz Fernández");

// Defines de constantes del módulo
#define MAX_SIZE 128 
// Pendiente porque aquí no hay un puntero
static struct proc_dir_entry *proc_entry; /*Información acerca del archivo creado en /proc*/

struct list_head numlist; /* Nodo fantasma (cabecera) de la lista enlazada */

/* Estructura que representa los nodos de la lista */
struct list_item {
    int data;
    struct list_head links;
};

/* Bindeamos las funcioens que hemos creado con las que aparecen en la interfaz del kernel */

/* Implementación de las funciones de lectura, escritura y clear del módulo */

/* Funcion que lee el espacio de usuario y escribe de manera segura en buf */
// TODO: PENDIENTE COMPROBAR SI ES CORRECTA
static ssize_t read_numlist (struct file *filp, char __user *buf, size_t len, loff_t *off) {
    
    int bytes_written;

    struct list_head *i = NULL;
    struct list_item *item = NULL;

    char bufaux[MAX_SIZE] = "";
    char *ptrbufaux = bufaux;

    list_for_each(i, &numlist) {
        // Usamos list_entry para saber el nodo que toca
        item = list_entry(i, struct list_item, links);
        
        int numberbytes = sprintf(ptrbufaux, "%d\n", item->data);

        // Comprobamos si podemos meter el numero dentro del buffer de char que hemos declarado
        if ((ptrbufaux - bufaux) + numberbytes > sizeof(char) * MAX_SIZE) {
            return -ENOSPC;
        }

        ptrbufaux += numberbytes;
    }

    ptrbufaux = '\0';

    bytes_written = ptrbufaux - bufaux;

    // si no podemos escribir en el buffe, lanzamos el error
    if (len < bytes_written) {
        return -ENOMEM;
    }

    if (copy_to_user(buf, bufaux, bytes_written)) {
        return -EINVAL;
    }

    (*off) += (bytes_written);

    return bytes_written;
}

/*Funcion que escribe los numeros hacia la estructura de datos*/
static ssize_t write_numlist (struct file *filp, const char __user *buf, size_t len, loff_t *off) {

    // numero que insertaremos en la lista
    int n; 
    char bufaux[MAX_SIZE];
    if (copy_from_user(bufaux, buf, len + 1) == 1) {
        return -EFAULT;
    }
    bufaux[len] = '\0';

    // guardo un puntero del elemento borrado
    struct list_head *elemborrado = NULL;

    // guardo un puntero del iterador de la lista
    struct list_head *iterator = NULL;

    // necesario cuando queremos hacer el kfree del puntero del elemento
    struct list_item *number;

    if (sscanf(bufaux, "add %i", &n) == 1) {
        struct list_item *newelem = (struct list_item*) kmalloc(sizeof(struct list_item), GFP_KERNEL);
        newelem->data = n;
        list_add_tail(&(newelem->links), &numlist);
    } 

    else if (sscanf(bufaux, "remove %i", &n) == 1) {
        // recorro la lista de números
        list_for_each_safe(iterator, elemborrado, &numlist) {
            number = list_entry(iterator, struct list_item, links);
            if (number->data == n) {
                number = list_entry(iterator, struct list_item, links);
                list_del(iterator);
                kfree(number);
                printk(KERN_INFO "El elemento %d ha sido borrado\n", n);
            }
        }
    } 

    else if (strcmp(bufaux, "cleanup\n") == 0){
        list_for_each_safe(iterator, elemborrado, &numlist) {
            number = list_entry(iterator, struct list_item, links);
            list_del(iterator);
            kfree(number);
        }
        printk(KERN_INFO "Todos los elementos han sido borrados\n");
    } 

    else 
        return -EINVAL; // pendiente cambio al error adecuado
    
    *off += len;
    // En caso de exito retorna cero
    return 0;
}   

struct proc_ops numlist_ops = {
    .proc_read = read_numlist, //read()
    .proc_write = write_numlist, //write()
};

/* Función que se invoca cuando se carga el módulo en el kernel */
int modlist_init(void) {
    // REVISAR
    INIT_LIST_HEAD(&numlist);

    proc_entry = proc_create ("modlist", 0666, NULL, &numlist_ops);

    if (proc_entry == NULL) {
        printk(KERN_INFO "Can't create the proc entry\n");
        return -ENOMEM;
    }

    printk(KERN_INFO "Modulo cargado correctamente.\n");
    /* Devolver 0 para indicar una carga correcta del módulo */
    return 0;
}

/* Función que se invoca cuando se descarga el módulo del kernel */
void modlist_clean(void) {
    remove_proc_entry("modlist", NULL);
    printk(KERN_INFO "Modulo LIN descargado. Adios kernel.\n");
}



/* Declaración de funciones init y cleanup */
module_init(modlist_init);
module_exit(modlist_clean);