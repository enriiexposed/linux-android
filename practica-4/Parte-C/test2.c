#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define DEVICE "/dev/display7s"
#define THREAD_COUNT 5
#define TEST_CHAR 'A'

const char vec[5][2] = {{'A'}, {'B'}, {'C'}, {'D'}, {'E'}};

// Descriptor de archivo compartido entre los hilos
int shared_fd;

// Función que cada hilo ejecutará
void* thread_function(void* arg) {
    int thread_id = *(int*)arg;
    char buffer[1] = { TEST_CHAR };

    printf("Hilo %d: escribiendo '%c' en el dispositivo\n", thread_id, vec[thread_id][0]);

    // Escribir en el dispositivo
    if (write(shared_fd, &vec[thread_id], 2*sizeof(char)) < 0) {
        perror("Error escribiendo en el dispositivo");
    }

    printf("Hilo %d: escritura completada\n", thread_id);
    pthread_exit(NULL);
}

int main() {
    pthread_t threads[THREAD_COUNT];
    int thread_ids[THREAD_COUNT];
    int i;

    // Abrir el dispositivo una vez
    shared_fd = open(DEVICE, O_WRONLY);
    if (shared_fd < 0) {
        perror("Error abriendo el dispositivo");
        return 1;
    }

    printf("Dispositivo abierto exitosamente.\n");

    // Crear múltiples hilos
    for (i = 0; i < THREAD_COUNT; i++) {
        thread_ids[i] = i;
        if (pthread_create(&threads[i], NULL, thread_function, &thread_ids[i]) != 0) {
            perror("Error creando hilo");
            close(shared_fd); // Asegurarse de cerrar el dispositivo
            return 1;
        }

	wait(1000);
    }

    // Esperar a que todos los hilos terminen
    for (i = 0; i < THREAD_COUNT; i++) {
        pthread_join(threads[i], NULL);
    }

    // Cerrar el dispositivo después de que todos los hilos hayan terminado
    close(shared_fd);

    printf("Todos los hilos han terminado. Dispositivo cerrado.\n");
    return 0;
}
