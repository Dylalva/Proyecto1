# Proyecto 1 Sistemas Operativos: Sistema Distribuido de Broker de Mensajes
---
## Integrantes del Grupo
<center>

| Nombre                   | Cédula          |
|--------------------------|-----------------|
| Dylan Elizondo Alvarado  | 504610652       |
| Luis David Salgado Gamez | 208670670       |
| Rodrigo Ureña Castillo   | 118910482       |

---
![Mensaje Broker](https://substackcdn.com/image/fetch/f_auto,q_auto:good,fl_progressive:steep/https%3A%2F%2Fsubstack-post-media.s3.amazonaws.com%2Fpublic%2Fimages%2Fb6eab95c-f56f-45c7-9248-7e6070c1979b_1614x702.png)

Este proyecto es una implementación en C de un sistema distribuido de broker de mensajes inspirado en Apache Kafka. Su finalidad es aprender a manejar múltiples procesos y técnicas de IPC, comunicación vía sockets TCP, concurrencia, sincronización y prevención de interbloqueos, empleando mecanismos como pipes, hilos, memoria compartida y semáforos.

---
# Indice
- [Funciones Principales de un Message Broker](#Funciones-Principales-de-un-Message-Broker)
- [Características del Sistema](#características-del-sistema)
- [Compilación y Ejecución](#compilación-y-ejecución)
    -   [Compilación](#compilación)
    - [Ejecuación](#ejecución)
- [Fuentes y Referencias para el Proyecto](#fuentes-y-referencias-para-el-proyecto)
    - [Uso de Archivos](#manejo-de-archivos)
    - [Pipes](#pipes)
    - [Hilos](#hilos)
    - [Memoria Compartida](#memoria-compartida)
    - [Message Broker](#message-broker)
    - [DeadLock](#deadlock)
    - [Proyecto de Ejemplo](#proyecto-de-ejemplo)

---
## Funciones Principales de un Message Broker

- **Recepción de Mensajes:** El broker actúa como receptor de mensajes enviados por diversos productores, manejando datos, eventos o comandos.
- **Almacenamiento y Encolamiento:** Los mensajes recibidos se almacenan en una cola o log, preservando el orden de llegada y permitiendo el procesamiento asíncrono.
- **Distribución:** Distribuye los mensajes a uno o varios consumidores, pudiendo gestionar diferentes grupos para balancear la carga y asegurar que cada mensaje sea procesado según la lógica de negocio.
- **Desacoplamiento:** Permite que los productores y consumidores operen de forma independiente, ya que no necesitan conocer la ubicación o la existencia directa del otro.

---

## Características del Sistema

- **Cola de Mensajes:** Implementación de una cola que registra y ordena los mensajes de acuerdo a su llegada.
- **Persistencia de Datos:** Almacenamiento en un archivo de log para el historial y orden de mensajes.
- **Concurrencia:** Manejo de múltiples conexiones TCP simultáneas, tanto para productores como para consumidores.
- **Grupos de Consumidores:** Capacidad de formar grupos para balancear la carga y garantizar que cada mensaje sea procesado una sola vez dentro de cada grupo.
- **Prevención de Deadlocks:** Uso de estrategias como orden fijo de adquisición de locks, tiempos de espera, trylock, y opcionalmente el algoritmo del banquero.

---

## Compilación y Ejecución

### Requisitos
- Compilador GCC compatible con el estándar de C.
- Sistema operativo Linux.

### Compilación

Utiliza el siguiente comando para compilar el proyecto:

```bash
gcc -o broker Broker.c -lpthread 
```

```bash
gcc -o consumer Consumer.c -lpthread 
```

```bash
gcc -o producer Producer.c -lpthread 
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

## Fuentes y Referencias para el Proyecto.

Esta sección recoge las fuentes consultadas para el desarrollo del proyecto, clasificados según cada tema.

### Manejo de Archivos
- [Programación en C - Manejo de Archivos (Wikibooks)](https://es.wikibooks.org/wiki/Programaci%C3%B3n_en_C/Manejo_de_archivos)

### Pipes
- [Pipes en C en Linux (programacion.com.py)](https://www.programacion.com.py/escritorio/c/pipes-en-c-linux)
- [Comunicación entre Pipes en C (StackOverflow en español)](https://es.stackoverflow.com/questions/310188/comunicaci%C3%B3n-entre-pipes-en-c)

### Hilos
- [Uso de memoria compartida en CUDA C/C++ (Developer NVIDIA)](https://developer-nvidia-com.translate.goog/blog/using-shared-memory-cuda-cc/?_x_tr_sl=en&_x_tr_tl=es&_x_tr_hl=es&_x_tr_pto=tc)

### Memoria Compartida
- [Programación paralela en C: Memoria compartida (Somos Binarios)](https://www.somosbinarios.es/programacion-paralela-en-c-memoria-compartida/)
- [Uso de memoria compartida en Linux en C (StackOverflow en inglés traducido)](https://stackoverflow-com.translate.goog/questions/5656530/how-to-use-shared-memory-with-linux-in-c?_x_tr_sl=en&_x_tr_tl=es&_x_tr_hl=es&_x_tr_pto=tc)

### Message Broker
- [IBM Think: Message Brokers](https://www.ibm.com/think/topics/message-brokers)
- [Cómo construir una Message Queue en C (Medium)](https://gotz.medium.com/how-to-build-a-good-enough-message-queue-in-c-a-non-production-ready-guide-6f1f222b02aa)

### DeadLock
- [Ejemplo de DeadLock en C (Gist)](https://gist.github.com/rouxcaesar/9b16d37789decddcab65dec6916264f2)
- [Depuración de Deadlock con Pthreads en Linux (Medium)](https://medium.com/@dastuam/debugging-basic-pthreads-deadlock-under-linux-with-gdb-928126694266)
- [Programa para crear Deadlock en C en Linux (Dextutor)](https://dextutor.com/program-to-create-deadlock-using-c-in-linux/)

---

