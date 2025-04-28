#!/bin/bash

# Nombre del archivo fuente y ejecutable
SOURCE_FILE="Broker.c"
EXECUTABLE="broker"

# Directorio del proyecto
PROJECT_DIR="/home/david/Documents/GitHub/Proyecto1/src"

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

# Ejecutar el broker en segundo plano
echo "Ejecutando el broker en segundo plano..."
./"$EXECUTABLE" &
BROKER_PID=$!

# Guardar el PID del proceso en un archivo
echo "$BROKER_PID" > broker.pid
echo "Broker ejecutándose en segundo plano con PID: $BROKER_PID"
