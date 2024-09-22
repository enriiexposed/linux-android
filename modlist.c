#include <linux/module.h> /* Requerido por todos los módulos */
#include <linux/kernel.h> /* Definición de KERN_INFO */
#include <linux/kmalloc.h>
#include <linux/proc_fs.h>
#include <linux/list.h>

MODULE_LICENSE("GPL"); /* Licencia del módulo */
MODULE_DESCRIPTION("Módulo del Kernel para insertar numeros en una lista");
MODULE_AUTHOR("Enrique Ríos Ríos");
MODULE_AUTHOR("Alejandro Orgaz Fernández");

// Defines de constantes del módulo
#define MAX_ELEMENTS 30

// Pendiente porque aquí no hay un puntero
struct proc_dir_entry *proc_entry /*Información acerca del archivo creado en /proc*/

struct list_head *numlist; /* Nodo fantasma (cabecera) de la lista enlazada */
int nelems = 0; // numero de elementos que tenemos en la lista actualmente

/* Estructura que representa los nodos de la lista */
struct list_item {
    int data;
    struct list_head links;
};

/* Bindeamos las funcioens que hemos creado con las que aparecen en la interfaz del kernel */
struct proc_ops numlist_ops = {
    .proc_read = read_numlist, //read()
    .proc_write = write_numlist, //write()
};

/* Implementación de las funciones de lectura, escritura y clear del módulo */

/* Funcion que lee el espacio de usuario y escribe de manera segura en buf */
// TODO: PENDIENTE COMPROBAR SI ES CORRECTA
static ssize_t read_numlist (struct file *filp, int __user *buf, size_t len, loff_t *off) {
    
    int bytes_written = sizeof(int)*nelems;

    // si no podemos escribir en el buffe, lanzamos el error
    if (len < bytes_written) {
        return -ENOMEM;
    }

    list_head *i = NULL;
    list_item *item = NULL;

    // creamos una lista auxiliar;
    int *aux;
    aux = kmalloc(bytes_written);

    i = 0;
    list_for_each(i, &numlist) {
        // Usamos list_entry para saber el nodo que toca
        item = list_entry(i, struct list_item, links);
        aux[i++] = item->data;  
    }

    if (copy_to_user(buf, aux, bytes_written) {
        return -EINVAL;
    }

    kfree(aux);

    (*off) += len;

    return bytes_written;
}

/*Funcion que escribe los numeros hacia la estructura de datos*/
static ssize_t write_numlist (struct file *filp, const int __user *buf, size_t len, loff_t *off) {

}   



/* Función que se invoca cuando se carga el módulo en el kernel */
int modlist_init(void) {
    // REVISAR
    INIT_LIST_HEAD(&numlist);
    // No sé si es necesario
    LIST_NAME("modlist");

    // RESERVO MEMORIA PARA CADA UNO DE LOS NODOS ?


    
    if (*numlist == NULL) {
        return -ENOMEM;
    }

    proc_entry = proc_create ("modlist", 0666, NULL, &numlist_ops);

    if (proc_entry == NULL) {
        ret = -ENOMEM;
        vfree(&numlist);
        printk(KERN_INFO "Can't create the proc entry\n");
    }

    printk(KERN_INFO "Modulo cargado correctamente.\n");
    /* Devolver 0 para indicar una carga correcta del módulo */
    return 0;
}

/* Función que se invoca cuando se descarga el módulo del kernel */
void modlist_clean(void) {
    remove_proc_entry("modlist", NULL);
    kfree(&numlist);
    printk(KERN_INFO "Modulo LIN descargado. Adios kernel.\n");
}


/* Declaración de funciones init y cleanup */
module_init(modlist_init);
module_exit(modlist_clean);