# Proyecto 1 Sistemas Operativos: Sistema Distribuido de Broker de Mensajes

---

## Integrantes del Grupo


| Nombre                   | Cédula    |
| ------------------------ | --------- |
| Dylan Elizondo Alvarado  | 504610652 |
| Luis David Salgado Gamez | 208670670 |
| Rodrigo Ureña Castillo   | 118910482 |

---

![Mensaje Broker](https://substackcdn.com/image/fetch/f_auto,q_auto\:good,fl_progressive\:steep/https%3A%2F%2Fsubstack-post-media.s3.amazonaws.com%2Fpublic%2Fimages%2Fb6eab95c-f56f-45c7-9248-7e6070c1979b_1614x702.png)

Este proyecto es una implementación en C de un sistema distribuido de broker de mensajes inspirado en Apache Kafka. Su finalidad es aprender a manejar múltiples procesos y técnicas de IPC, comunicación vía sockets TCP, concurrencia, sincronización y prevención de interbloqueos, empleando mecanismos como pipes, hilos, memoria compartida y semáforos.

---

# Indice

* [Estructura del proyecto](#Estructura-del-proyecto)
* [Funciones Principales de un Message Broker](#Funciones-Principales-de-un-Message-Broker)
* [Características del Sistema](#características-del-sistema)
* [Descripción del sistema implementado](#Descripción-del-Sistema-Implementado)
* [Compilación y Ejecución](#compilación-y-ejecución)

  * [Compilación](#compilación)
  * [Ejecuación](#ejecución)
 
* [Estrategia utilizada para evitar interbloqueos](Estrategia-Utilizada-para-Evitar-Interbloqueos)
* [Limitaciones](#Limitaciones)
* [Uso del Makefile](#Uso-del-Makefile)
  * [Requisitos](#Requisitos)
  * [Instrucciones](#Instrucciones)

* [Fuentes y Referencias para el Proyecto](#fuentes-y-referencias-para-el-proyecto)

  * [Uso de Archivos](#manejo-de-archivos)
  * [Pipes](#pipes)
  * [Hilos](#hilos)
  * [Memoria Compartida](#memoria-compartida)
  * [Message Broker](#message-broker)
  * [DeadLock](#deadlock)
  * [Manejo de Sockets en C](#Manejo-de-Sockets-en-C)
  * [Depuración y Manejo de Memoria](#Depuración-y-Manejo-de-Memoria)
  * [Algoritmo del Banquero](#Algoritmo-del-Banquero)

---

## Estructura del proyecto
```
PROYECTO1/
├── src/
│ ├── broker.c
│ ├── consumer.c
│ ├── producer.c
├── Makefile
├── README.md
```

---

## Funciones Principales de un Message Broker

* **Recepción de Mensajes:** El broker actúa como receptor de mensajes enviados por diversos productores, manejando datos, eventos o comandos.
* **Almacenamiento y Encolamiento:** Los mensajes recibidos se almacenan en una cola o log, preservando el orden de llegada y permitiendo el procesamiento asíncrono.
* **Distribución:** Distribuye los mensajes a uno o varios consumidores, pudiendo gestionar diferentes grupos para balancear la carga y asegurar que cada mensaje sea procesado según la lógica de negocio.
* **Desacoplamiento:** Permite que los productores y consumidores operen de forma independiente, ya que no necesitan conocer la ubicación o la existencia directa del otro.

---

## Características del Sistema

* **Cola de Mensajes:** Implementación de una cola que registra y ordena los mensajes de acuerdo a su llegada.
* **Persistencia de Datos:** Almacenamiento en un archivo de log para el historial y orden de mensajes.
* **Concurrencia:** Manejo de múltiples conexiones TCP simultáneas, tanto para productores como para consumidores.
* **Grupos de Consumidores:** Capacidad de formar grupos para balancear la carga y garantizar que cada mensaje sea procesado una sola vez dentro de cada grupo.
* **Prevención de Deadlocks:** Uso de estrategias como orden fijo de adquisición de locks, tiempos de espera, trylock, y opcionalmente el algoritmo del banquero.

---

## Descripción del Sistema Implementado

Este código implementa un sistema distribuido de *message broker* que permite la comunicación asíncrona entre productores y consumidores de mensajes, similar a sistemas como Apache Kafka. El broker recibe mensajes de productores a través de sockets TCP, los encola y los distribuye a los consumidores conectados.

Las características principales incluyen:

1. **Cola de Mensajes**: Utiliza una cola con semáforos para gestionar el encolamiento y desencolado de mensajes.
2. **Distribución de Mensajes**: Los mensajes son enviados a los consumidores a través de un sistema de grupos, donde cada consumidor recibe un mensaje de manera equitativa.
3. **Persistencia**: Los mensajes son almacenados en un archivo de log para asegurar la trazabilidad.
4. **Concurrencia**: Se manejan múltiples conexiones simultáneas mediante hilos y el uso adecuado de mutexes y semáforos para evitar condiciones de carrera.
5. **Manejo de Fallos**: El sistema maneja de manera eficiente la eliminación de consumidores caídos y el reenvío de mensajes si un intento de envío falla.

## Compilación y Ejecución

### Requisitos

* Compilador GCC compatible con el estándar de C.
* Sistema operativo Linux.

### Compilación

Utiliza el siguiente comando para compilar el proyecto:

```bash
gcc -o broker broker.c -lpthread 
```

```bash
gcc -o consumer consumer.c -lpthread 
```

```bash
gcc -o producer producer.c -lpthread 
```

### Ejecución

Inicia el broker:

```bash
./broker
```

Inicia los productores (en terminales separadas o en background):

```bash
./producer
```

Inicia los consumidores:

```bash
./consumer
```

> **Nota:** Asegúrate de que el broker esté corriendo antes de iniciar los clientes.

---

## Estrategia Utilizada para Evitar Interbloqueos

El sistema implementa varias estrategias para evitar interbloqueos:

1. **Mutexes de Protección**: Se emplean mutexes para garantizar el acceso exclusivo a recursos compartidos, como las colas de mensajes y las listas de consumidores. Esto previene las condiciones de carrera que podrían generar interbloqueos.
2. **Orden de Adquisición de Locks**: El sistema sigue un orden predefinido para adquirir mutexes, evitando ciclos de espera que son la causa principal de los interbloqueos.
3. **Semáforos**: Se utiliza un semáforo para controlar el tamaño de la cola, lo que garantiza que no se añadan más mensajes de los que pueden ser procesados, evitando la congestión que podría llevar a un interbloqueo.
4. **Reintentos Automáticos**: Si un consumidor no puede recibir un mensaje debido a un error de conexión, se intenta reenviar el mensaje hasta tres veces antes de eliminar al consumidor defectuoso.

## Limitaciones

1. **Escalabilidad**: El sistema está limitado a manejar un número pequeño de consumidores debido a la forma en que se gestionan los grupos de consumidores. No hay una optimización para escalabilidad horizontal en servidores distribuidos.
2. **Manejo de Errores**: Aunque el sistema maneja ciertos errores (como la desconexión de consumidores), no tiene una estrategia robusta para manejar fallos a gran escala, como caídas de varios productores o consumidores.
3. **Persistencia de Mensajes**: Aunque los mensajes se registran en un log, no hay un mecanismo de recuperación en caso de fallo del broker, lo que podría resultar en pérdida de datos.

---

## Uso del Makefile

### Requisitos

1. **Sistema Operativo**: Linux.
2. **Instalar GCC y Make**:

   ```bash
   sudo apt update
   sudo apt install build-essential
   ```

### Instrucciones

1. **Compilar el Proyecto**:
   En la carpeta donde se encuentra el Makefile, ejecuta:

   ```bash
   make
   ```

   Esto generará los ejecutables:

   * `broker`: El servidor principal.
   * `producer`: El productor de mensajes.
   * `consumer`: El consumidor de mensajes.

2. **Limpiar Archivos Compilados**:

   ```bash
   make clean
   ```

   Esto eliminará los ejecutables generados.

>**Nota** Este Makefile automatiza la compilación de los ejecutables y la limpieza de archivos generados.

--- 


## Fuentes y Referencias para el Proyecto

Esta sección recoge las fuentes consultadas para el desarrollo del proyecto, clasificados según cada tema.

### Manejo de Archivos

* [Programación en C - Manejo de Archivos (Wikibooks)](https://es.wikibooks.org/wiki/Programaci%C3%B3n_en_C/Manejo_de_archivos)

### Pipes

* [Pipes en C en Linux (programacion.com.py)](https://www.programacion.com.py/escritorio/c/pipes-en-c-linux)
* [Comunicación entre Pipes en C (StackOverflow en español)](https://es.stackoverflow.com/questions/310188/comunicaci%C3%B3n-entre-pipes-en-c)

### Hilos

* [Uso de memoria compartida en CUDA C/C++ (Developer NVIDIA)](https://developer-nvidia-com.translate.goog/blog/using-shared-memory-cuda-cc/?_x_tr_sl=en&_x_tr_tl=es&_x_tr_hl=es&_x_tr_pto=tc)
* * [Man7: Pthreads - Manual](https://man7.org/linux/man-pages/man7/pthreads.7.html)

### Memoria Compartida

* [Programación paralela en C: Memoria compartida (Somos Binarios)](https://www.somosbinarios.es/programacion-paralela-en-c-memoria-compartida/)
* [Uso de memoria compartida en Linux en C (StackOverflow en inglés traducido)](https://stackoverflow-com.translate.goog/questions/5656530/how-to-use-shared-memory-with-linux-in-c?_x_tr_sl=en&_x_tr_tl=es&_x_tr_hl=es&_x_tr_pto=tc)

### Message Broker

* [IBM Think: Message Brokers](https://www.ibm.com/think/topics/message-brokers)
* [Cómo construir una Message Queue en C (Medium)](https://gotz.medium.com/how-to-build-a-good-enough-message-queue-in-c-a-non-production-ready-guide-6f1f222b02aa)

### DeadLock

* [Ejemplo de DeadLock en C (Gist)](https://gist.github.com/rouxcaesar/9b16d37789decddcab65dec6916264f2)
* [Depuración de Deadlock con Pthreads en Linux (Medium)](https://medium.com/@dastuam/debugging-basic-pthreads-deadlock-under-linux-with-gdb-928126694266)
* [Programa para crear Deadlock en C en Linux (Dextutor)](https://dextutor.com/program-to-create-deadlock-using-c-in-linux/)

### Manejo de Sockets en C
* [GeeksforGeeks: Socket Programming in C](https://www.geeksforgeeks.org/socket-programming-cc/)
* [Dev.to: A Beginner’s Guide to Socket Programming in C](https://dev.to/sanjayrv/a-beginners-guide-to-socket-programming-in-c-5an5)

### Depuración y Manejo de Memoria
* [Valgrind: Quick Start](https://valgrind-org.translate.goog/docs/manual/quick-start.html?_x_tr_sl=en&_x_tr_tl=es&_x_tr_hl=es&_x_tr_pto=tc)

### Algoritmo del Banquero

* [GeeksforGeeks: Banker's Algorithm in Operating System](https://www-geeksforgeeks-org.translate.goog/bankers-algorithm-in-operating-system-2/?_x_tr_sl=en&_x_tr_tl=es&_x_tr_hl=es&_x_tr_pto=tc)
* [Wikipedia: Algoritmo del Banquero](https://es.wikipedia.org/wiki/Algoritmo_del_banquero)
