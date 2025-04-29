#!/bin/bash

# Nombre del archivo fuente y ejecutable
SOURCE_FILE="Consumer.c"
EXECUTABLE="Consumer"

# Directorio del proyecto
PROJECT_DIR="/home/rodrigo/Proyecto1/src"

# Cambiar al directorio del proyecto
cd "$PROJECT_DIR" || { echo "Error: No se pudo acceder al directorio $PROJECT_DIR"; exit 1; }

# Compilar el archivo fuente
echo "Compilando $SOURCE_FILE..."
gcc -o "$EXECUTABLE" "$SOURCE_FILE" -lpthread -lrt
if [ $? -ne 0 ]; then
    echo "Error: Falló la compilación de $SOURCE_FILE"
    exit 1
fi
echo "Compilación exitosa."

# Ejecutar 10 instancias del consumidor en pestañas separadas
echo "Ejecutando 10 instancias del consumidor en pestañas separadas..."
for i in {1..4}; do
    gnome-terminal --tab --title="Consumer $i" -- bash -c "./$EXECUTABLE; exec bash"
done

echo "Se han iniciado 10 consumidores."
