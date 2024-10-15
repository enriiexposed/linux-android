#include <linux/syscalls.h> /* For SYSCALL_DEFINEi() */
#include <linux/kernel.h>
#include <asm-generic/errno.h>
#include <linux/module.h>
#include <linux/tty.h>

/*
Falta meter registro en la tabla de arquitectura CUANDO COMPILEMOS KERNEL
*/

struct tty_driver* kbd_driver = NULL;

SYSCALL_DEFINE1(ledctl, unisgned int leds)
{
  if (leds < 0 || leds > 7) {
    printk(KERN_INFO "La mascara de bits solo admite numeros entre 0 y 7");
    return -EINVAL;
  }

  if (kbd_driver == NULL) init_driver();

  return set_leds(kbd_driver, leds);
}

void init_driver() {
  kbd_driver = vc_cons[fg_console].d->port.tty->driver;
}

static inline  int set_leds(struct tty_driver* handler, unsigned int mask) {
  return (handler->ops->ioctl) (vc_cons[fg_console].d->port.tty, KDSETLED,mask);
}
