# Este Makefile es sólo para un archivo de código c

obj-m = mi_modulo.o
all:
make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean