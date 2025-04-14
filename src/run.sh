#!/bin/bash

# Compilar los archivos .c
echo "Compilando archivos..."
gcc -o Consumer Consumer.c -lpthread
gcc -o Producer Producer.c -lpthread

# Verificar si la compilación fue exitosa
if [[ $? -ne 0 ]]; then
    echo "Error al compilar los archivos. Revisa el código."
    exit 1
fi

# Esperar un momento para asegurarse de que el broker esté activo
sleep 2

# Lanzar 25 consumidores
echo "Lanzando 25 consumidores..."
for i in {1..25}
do
    ./Consumer &
done

# Esperar un momento para que los consumidores se conecten
sleep 2

# Enviar 100 mensajes desde los productores
echo "Enviando 100 mensajes desde los productores..."
for i in {1..10000}
do
    ./Producer &
done

# Esperar a que terminen todos los procesos
wait $BROKER_PID
echo "Ejecución completada."