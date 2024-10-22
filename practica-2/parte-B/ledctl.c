#include <linux/syscalls.h> /* For SYSCALL_DEFINEi() */
#include <linux/kernel.h>
#include <asm-generic/errno.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/init.h>
#include <linux/kd.h>
#include <linux/vt_kern.h>

#define LED_SCROLL_LOCK 0x1  // Bit 0: Scroll Lock
#define LED_NUM_LOCK    0x2  // Bit 1: Num Lock
#define LED_CAPS_LOCK   0x4  // Bit 2: Caps Lock
/*
Falta meter registro en la tabla de arquitectura CUANDO COMPILEMOS KERNEL
*/

struct tty_driver* kbd_driver = NULL;

/* Get driver handler */
struct tty_driver* get_kbd_driver_handler(void){
   return vc_cons[fg_console].d->port.tty->driver;
}

static inline  int set_leds(struct tty_driver* handler, unsigned int mask) {
  return (handler->ops->ioctl) (vc_cons[fg_console].d->port.tty, KDSETLED,mask);
}

SYSCALL_DEFINE1(ledctl, unsigned int, leds)
{
  if (leds < 0 || leds > 7) {
    printk(KERN_INFO "La mascara de bits solo admite numeros entre 0 y 7");
    return -EINVAL;
  }

  kbd_driver= get_kbd_driver_handler();
  unsigned int mask = ((leds & 0x4) >>1) | ((leds & 0x2) << 1) | (leds & 0x1);
  if (set_leds(kbd_driver, mask) !=  0) {
    printk(KERN_INFO "Es posible que esta llamada al sistema haya sido ejecutada sin ser root\n");
    return -EACCES;
  }

  return 0;
}
