#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define DEVICE_PATH "/dev/usb/blinkstick0"
#define NR_LEDS 8

void write_leds(int fd) {
    char buffer[256];
    unsigned int color;
    int i;

    // Encender LEDs en una secuencia
    for (int cycle = 0; cycle < 3; cycle++) {
        // Encender de 0 a 7
        for (i = 0; i < NR_LEDS; i++) {
            color = 0x990000; // Rojo 
            snprintf(buffer, sizeof(buffer), "%u:0x%06X", i, color);
            if (write(fd, buffer, strlen(buffer)) < 0) {
                perror("Error al escribir en el dispositivo");
                return;
            }
            usleep(100000); // Espera medio segundo
        }

        // Rebote de 7 a 0
        for (i = NR_LEDS - 1; i >= 0; i--) {
            color = 0x009900; // Verde
            snprintf(buffer, sizeof(buffer), "%u:0x%06X", i, color);
            if (write(fd, buffer, strlen(buffer)) < 0) {
                perror("Error al escribir en el dispositivo");
                return;
            }
            usleep(100000); // Espera medio segundo
        }
    }
}

int main() {
    int fd = open(DEVICE_PATH, O_WRONLY);
    if (fd < 0) {
        perror("No se pudo abrir el dispositivo");
        return EXIT_FAILURE;
    }

    write_leds(fd);
    
    close(fd);
    return EXIT_SUCCESS;
}

