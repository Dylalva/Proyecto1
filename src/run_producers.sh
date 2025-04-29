#!/bin/bash

# Nombre del archivo fuente y ejecutable
SOURCE_FILE="Producer.c"
EXECUTABLE="Producer"

# Directorio del proyecto
PROJECT_DIR="/home/rodrigo/Proyecto1/src"

# Cambiar al directorio del proyecto
cd "$PROJECT_DIR" || { echo "Error: No se pudo acceder al directorio $PROJECT_DIR"; exit 1; }

# Compilar el archivo fuente
echo "Compilando $SOURCE_FILE..."
gcc -o "$EXECUTABLE" "$SOURCE_FILE" -lpthread
if [ $? -ne 0 ]; then
    echo "Error: Falló la compilación de $SOURCE_FILE"
    exit 1
fi
echo "Compilación exitosa."

# Ejecutar 100 instancias del productor con un intervalo de 1 segundo
echo "Ejecutando 100 instancias del productor..."
for i in {1..100}; do
    echo "Iniciando Producer $i..."
    ./"$EXECUTABLE" &
    sleep 0.3
done

echo "Se han iniciado 100 productores."
