#include <linux/module.h> /* Requerido por todos los módulos */
#include <linux/kernel.h> /* Definición de KERN_INFO */
MODULE_LICENSE("GPL"); /* Licencia del módulo */

struct list_head mylist; /* Nodo fantasma (cabecera) de la lista enlazada */

/* Estructura que representa los nodos de la lista */
struct list_item {
    int data;
    struct list_head links;
};

/* Función que se invoca cuando se carga el módulo en el kernel */
int modlist_init(void) {
    printk(KERN_INFO "Modulo LIN cargado. Hola kernel.\n");
    /* Devolver 0 para indicar una carga correcta del módulo */
    return 0;
}

/* Función que se invoca cuando se descarga el módulo del kernel */
void modlist_clean(void) {
    printk(KERN_INFO "Modulo LIN descargado. Adios kernel.\n");
}


/* Declaración de funciones init y cleanup */
module_init(modlist_init);
module_exit(modlist_clean);