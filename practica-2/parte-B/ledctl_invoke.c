#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>

#define SYS_ledctl 451 // Sustituye 451 por el número asignado a la syscall ledctl en tu sistema.

int main(int argc, char* argv[]) {
    if (argc != 2) {
        perror("Argumentos Incorrectos\n");
        return EXIT_FAILURE;
    }

    errno = 0;

    char* cad;

    long int leds_mask = strtol(argv[1], &cad, 10);

    if (*cad != '\0') {
        perror("parametro no valido");
        return 1;
    }

    if (errno == EINVAL || errno == ERANGE) {
        perror("Ha habido un error\n");
        return -1;
    }

    errno = 0;

    long int res = syscall(SYS_ledctl, leds_mask);

    if (res == -1) {
        perror("Error en la llamada ledctl\n");
        return EXIT_FAILURE;
    } else {
        printf("LEDs cambiados correctamente\n");
        return 0;
    }
}
