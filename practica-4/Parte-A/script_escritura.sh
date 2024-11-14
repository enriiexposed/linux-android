#!/bin/bash

# Número máximo para añadir y eliminar en cada operación
MAX_NUM=3
WAIT=0.3
# Comienza el bucle infinito
for i in {1..100};do
    # Genera un número aleatorio entre 1 y MAX_NUM
    #num=$((RANDOM % MAX_NUM + 1))

    # Alterna entre agregar y eliminar el número en /proc/modlist
    echo "add $i" > /proc/modlist

    sleep $WAIT  # Espera un breve momento antes de la siguiente operación

    #echo "remove $MAX_NUM" > /proc/modlist

    #sleep $WAIT  # Espera un breve momento antes de la siguiente iteración
done

