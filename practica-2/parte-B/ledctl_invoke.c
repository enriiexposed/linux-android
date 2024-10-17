#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>

#define SYS_ledctl 451 // Sustituye 451 por el número asignado a la syscall ledctl en tu sistema.

int main(int argc, char* argv[]) {
    if (argc != 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    char* endptr;
    long int leds_mask = strtol(argv[1], &endptr, 10);

    if (endptr != "\0") {
        return 1;
    }
    long int res = syscall(SYS_ledctl, leds_mask);

    if (res == -1) {
        perror("Error en la llamada ledctl");
        return EXIT_FAILURE;
    }

    printf("LEDs actualizados correctamente.\n");
    return EXIT_SUCCESS;
}
