#!/bin/bash

for i in {1..20000}
do
    echo "Lanzando ./Producer en segundo plano #$i"
    ./Producer &
done

wait  # Espera a que terminen todos
echo "Todos los productores terminaron."
