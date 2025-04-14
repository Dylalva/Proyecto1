#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>

// --------------------
// Estructuras y cola
// --------------------

typedef struct Node {
    void *data;
    struct Node *next;
} Node;

typedef struct Queue {
    Node *front;
    Node *rear;
    int size;
} Queue;

Queue *initQueue() {
    Queue *queue = (Queue *)malloc(sizeof(Queue));
    if (!queue) {
        perror("Error al asignar memoria para la cola");
        exit(EXIT_FAILURE);
    }
    queue->front = NULL;
    queue->rear = NULL;
    queue->size = 0;
    return queue;
}

void enqueue(Queue *queue, void *data) {
    Node *newNode = (Node *)malloc(sizeof(Node));
    if (!newNode) {
        perror("Error al asignar memoria para el nodo");
        exit(EXIT_FAILURE);
    }
    newNode->data = data;
    newNode->next = NULL;
    if (queue->rear == NULL) {
        queue->front = newNode;
        queue->rear = newNode;
    } else {
        queue->rear->next = newNode;
        queue->rear = newNode;
    }
    queue->size++;
}

void *dequeue(Queue *queue) {
    if (queue->front == NULL) {
        return NULL;
    }
    Node *temp = queue->front;
    void *data = temp->data;
    queue->front = queue->front->next;
    if (queue->front == NULL) {
        queue->rear = NULL;
    }
    free(temp);
    queue->size--;
    return data;
}

int isEmpty(Queue *queue) {
    return queue->size == 0;
}

void freeQueue(Queue *queue) {
    while (!isEmpty(queue)) {
        dequeue(queue);
    }
    free(queue);
}

// --------------------
// Estructuras de mensaje y consumidores/producers
// --------------------

typedef struct {
    long offset;
    int id;
    char origen[50];
    char mensaje[256];
} Mensaje;

typedef struct {
    int id;
    int socket_fd;  // Descriptor del socket
    struct sockaddr_in direccion;
} Consumer;

typedef struct {
    Consumer **consumers;
    int count;
    int capacity;
} ConsumerList;

typedef struct {
    ConsumerList **groups;
    int count;
    int capacity;
} ConsumerGroupContainer;

// --------------------
// Funciones para manejar listas
// --------------------

ConsumerList *initConsumerList() {
    ConsumerList *list = malloc(sizeof(ConsumerList));
    list->count = 0;
    list->capacity = 5; // Máximo de 5 consumidores por grupo
    list->consumers = malloc(sizeof(Consumer *) * list->capacity);
    return list;
}

void addConsumer(ConsumerList *list, Consumer *consumer) {
    if (list->count == list->capacity) {
        printf("El grupo ya tiene el máximo de consumidores (5).\n");
        return;
    }
    list->consumers[list->count++] = consumer;
}

ConsumerGroupContainer *initConsumerGroupContainer() {
    ConsumerGroupContainer *container = malloc(sizeof(ConsumerGroupContainer));
    container->count = 0;
    container->capacity = 10;
    container->groups = malloc(sizeof(ConsumerList *) * container->capacity);
    return container;
}

void addConsumerGroup(ConsumerGroupContainer *container, ConsumerList *group) {
    if (container->count == container->capacity) {
        container->capacity *= 2;
        container->groups = realloc(container->groups, sizeof(ConsumerList *) * container->capacity);
    }
    container->groups[container->count++] = group;
}

// --------------------
// Variables globales
// --------------------

Queue *cola;
int mensaje_id = 0;
pthread_mutex_t cola_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t consumer_mutex = PTHREAD_MUTEX_INITIALIZER;

ConsumerGroupContainer *consumerGroups;

// --------------------
// Funciones de manejo de mensajes
// --------------------

void imprimirCola(Queue *queue) {
    printf("\n--- Mensajes en la Cola ---\n");
    Node *current = queue->front;
    while (current) {
        Mensaje *msg = (Mensaje *)current->data;
        printf("ID: %d | Origen: %s | Contenido: %s\n", msg->id, msg->origen, msg->mensaje);
        current = current->next;
    }
    printf("---------------------------\n");
}

void imprimirConsumers(ConsumerGroupContainer *container) {
    printf("\n--- Consumers Conectados ---\n");
    for (int i = 0; i < container->count; i++) {
        ConsumerList *group = container->groups[i];
        printf("Grupo %d:\n", i + 1);
        for (int j = 0; j < group->count; j++) {
            Consumer *consumer = group->consumers[j];
            printf("  Consumer ID: %d\n", consumer->id);
        }
    }
    printf("-----------------------------\n");
}

void enviarMensajeAConsumers(Mensaje *msg) {
    pthread_mutex_lock(&consumer_mutex);
    for (int i = 0; i < consumerGroups->count; i++) {
        ConsumerList *group = consumerGroups->groups[i];
        if (group->count > 0) {
            // Seleccionar un índice aleatorio dentro del grupo
            int random_index = rand() % group->count;
            Consumer *consumer = group->consumers[random_index];

            // Enviar el mensaje al consumidor seleccionado
            if (send(consumer->socket_fd, msg, sizeof(Mensaje), 0) < 0) {
                perror("Error al enviar el mensaje al consumer");
            } else {
                printf("Mensaje enviado al Consumer ID: %d del Grupo %d\n", consumer->id, i + 1);
            }
        }
    }
    pthread_mutex_unlock(&consumer_mutex);
}
// --------------------
// Manejo de conexiones
// --------------------

void *manejarConexionProducer(void *arg) {
    int socket_cliente = *(int *)arg;
    free(arg);

    Mensaje msg;
    ssize_t bytes_recibidos = recv(socket_cliente, &msg, sizeof(Mensaje), 0);

    if (bytes_recibidos > 0) {
        pthread_mutex_lock(&cola_mutex);
        msg.id = mensaje_id++;

        Mensaje *msg_ptr = malloc(sizeof(Mensaje));
        if (msg_ptr) {
            memcpy(msg_ptr, &msg, sizeof(Mensaje));
            enqueue(cola, msg_ptr);

            printf("\nMensaje recibido de Producer:\n");
            printf("  ID: %d\n  Origen: %s\n  Contenido: %s\n", msg.id, msg.origen, msg.mensaje);

            imprimirCola(cola);
            imprimirConsumers(consumerGroups);

            enviarMensajeAConsumers(msg_ptr);

            // Eliminar el mensaje de la cola después de enviarlo
            dequeue(cola);
        } else {
            perror("No se pudo asignar memoria para el mensaje");
        }
        pthread_mutex_unlock(&cola_mutex);
    } else {
        perror("Error al recibir mensaje o conexión cerrada");
    }

    close(socket_cliente);
    pthread_exit(NULL);
}

void *manejarConexionConsumer(void *arg) {
    int socket_cliente = *(int *)arg;
    free(arg);

    Consumer *consumer = malloc(sizeof(Consumer));
    consumer->id = mensaje_id++;
    consumer->socket_fd = socket_cliente;
    socklen_t addr_len = sizeof(consumer->direccion);
    getpeername(socket_cliente, (struct sockaddr *)&consumer->direccion, &addr_len);

    pthread_mutex_lock(&consumer_mutex);
    if (consumerGroups->count == 0 || consumerGroups->groups[consumerGroups->count - 1]->count == 5) {
        addConsumerGroup(consumerGroups, initConsumerList());
    }
    addConsumer(consumerGroups->groups[consumerGroups->count - 1], consumer);
    pthread_mutex_unlock(&consumer_mutex);

    printf("Nuevo Consumer conectado con ID: %d\n", consumer->id);
    pthread_exit(NULL);
}

// --------------------
// Hilos para manejar conexiones
// --------------------

void *hiloProducers(void *arg) {
    int socket_servidor = *(int *)arg;
    while (1) {
        struct sockaddr_in direccion_cliente;
        socklen_t tam_cliente = sizeof(direccion_cliente);
        int nueva_conexion = accept(socket_servidor, (struct sockaddr *)&direccion_cliente, &tam_cliente);
        if (nueva_conexion < 0) {
            perror("Error al aceptar conexión de Producer");
            continue;
        }

        int *socket_ptr = malloc(sizeof(int));
        *socket_ptr = nueva_conexion;

        pthread_t hilo;
        if (pthread_create(&hilo, NULL, manejarConexionProducer, socket_ptr) != 0) {
            perror("No se pudo crear hilo para manejar conexión de Producer");
            close(nueva_conexion);
            free(socket_ptr);
        } else {
            pthread_detach(hilo);
        }
    }
}

void *hiloConsumers(void *arg) {
    int socket_servidor = *(int *)arg;
    while (1) {
        struct sockaddr_in direccion_cliente;
        socklen_t tam_cliente = sizeof(direccion_cliente);
        int nueva_conexion = accept(socket_servidor, (struct sockaddr *)&direccion_cliente, &tam_cliente);
        if (nueva_conexion < 0) {
            perror("Error al aceptar conexión de Consumer");
            continue;
        }

        int *socket_ptr = malloc(sizeof(int));
        *socket_ptr = nueva_conexion;

        pthread_t hilo;
        if (pthread_create(&hilo, NULL, manejarConexionConsumer, socket_ptr) != 0) {
            perror("No se pudo crear hilo para manejar conexión de Consumer");
            close(nueva_conexion);
            free(socket_ptr);
        } else {
            pthread_detach(hilo);
        }
    }
}

// --------------------
// Función principal del broker
// --------------------

void iniciar_broker() {
    int socket_producers, socket_consumers;
    struct sockaddr_in direccion_producers, direccion_consumers;

    cola = initQueue();
    consumerGroups = initConsumerGroupContainer();

    // Configurar socket para Producers
    socket_producers = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_producers < 0) {
        perror("Error al crear el socket para Producers");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(socket_producers, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    direccion_producers.sin_family = AF_INET;
    direccion_producers.sin_port = htons(8081);
    direccion_producers.sin_addr.s_addr = INADDR_ANY;

    if (bind(socket_producers, (struct sockaddr *)&direccion_producers, sizeof(direccion_producers)) < 0) {
        perror("Error al hacer bind para Producers");
        close(socket_producers);
        exit(EXIT_FAILURE);
    }

    if (listen(socket_producers, 1000) < 0) {
        perror("Error al escuchar para Producers");
        close(socket_producers);
        exit(EXIT_FAILURE);
    }

    // Configurar socket para Consumers
    socket_consumers = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_consumers < 0) {
        perror("Error al crear el socket para Consumers");
        exit(EXIT_FAILURE);
    }

    setsockopt(socket_consumers, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    direccion_consumers.sin_family = AF_INET;
    direccion_consumers.sin_port = htons(8082);
    direccion_consumers.sin_addr.s_addr = INADDR_ANY;

    if (bind(socket_consumers, (struct sockaddr *)&direccion_consumers, sizeof(direccion_consumers)) < 0) {
        perror("Error al hacer bind para Consumers");
        close(socket_consumers);
        exit(EXIT_FAILURE);
    }

    if (listen(socket_consumers, 1000) < 0) {
        perror("Error al escuchar para Consumers");
        close(socket_consumers);
        exit(EXIT_FAILURE);
    }

    printf("Broker ACTIVO escuchando en los puertos 8081 (Producers) y 8082 (Consumers)...\n");

    pthread_t hilo_producers, hilo_consumers;
    pthread_create(&hilo_producers, NULL, hiloProducers, &socket_producers);
    pthread_create(&hilo_consumers, NULL, hiloConsumers, &socket_consumers);

    pthread_join(hilo_producers, NULL);
    pthread_join(hilo_consumers, NULL);

    freeQueue(cola);
    close(socket_producers);
    close(socket_consumers);
}

// --------------------
// Mecanismo de failover
// --------------------

int main() {
    int intento = 0;
    srand(time(NULL));
    while (1) {
        int socket_prueba = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_prueba < 0) {
            perror("Error creando socket de prueba");
            exit(EXIT_FAILURE);
        }

        struct sockaddr_in direccion;
        direccion.sin_family = AF_INET;
        direccion.sin_port = htons(8080);
        direccion.sin_addr.s_addr = INADDR_ANY;

        if (bind(socket_prueba, (struct sockaddr *)&direccion, sizeof(direccion)) == 0) {
            close(socket_prueba);
            printf("Este broker asume el rol ACTIVO en el intento #%d\n", intento + 1);
            iniciar_broker();
            break;
        } else {
            if (errno == EADDRINUSE) {
                printf("Puerto en uso. Otro broker está activo. Reintentando...\n");
            } else {
                perror("Error inesperado al hacer bind");
            }
        }

        close(socket_prueba);
        intento++;
        sleep(5);
    }

    return 0;
}