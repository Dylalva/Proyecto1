#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
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


// --------------------
// Estructuras de mensaje y consumers/producers
// --------------------

typedef struct {
    long offset;
    int id;
    char origen[50];
    char mensaje[256];
} Message;

typedef struct {
    int id;
    int socket_fd;  // Descriptor del socket
    struct sockaddr_in direccion;
} Consumer;

typedef struct {
    Consumer **consumers;
    int count;
    int capacity;
    int consumer_index;
    pthread_t thread_group;
} ConsumerGroup;

typedef struct ConsumerGroupNode {
    ConsumerGroup *group;
    struct ConsumerGroupNode *next;
} ConsumerGroupNode;

typedef struct {
    ConsumerGroupNode *head;
    pthread_mutex_t mutex;
} ConsumerGroupContainer;


// --------------------
// Variables globales
// --------------------

Queue *cola;
int mensaje_id = 0;
int consumer_id = 0;
pthread_mutex_t cola_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cola_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t consumer_mutex = PTHREAD_MUTEX_INITIALIZER;

#define MAX_QUEUE_SIZE 1000 // Límite máximo de mensajes en la cola
sem_t cola_sem; // Semáforo para controlar el tamaño de la cola
ConsumerGroupContainer *consumerGroups;
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

    sem_wait(&cola_sem);
    pthread_mutex_lock(&cola_mutex);

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

    printf("Mensaje agregado a la cola. Tamaño actual: %d\n", queue->size);

    pthread_mutex_unlock(&cola_mutex);
}

void *dequeue(Queue *queue) {
    pthread_mutex_lock(&cola_mutex);
    if (queue->front == NULL) {
        pthread_mutex_unlock(&cola_mutex);
        return NULL;
    }

    Node *temp = queue->front;
    void *data = temp->data;
    queue->front = queue->front->next;
    if (queue->front == NULL) {
        queue->rear = NULL;
    }
    queue->size--;

    // Incrementar el semáforo para liberar espacio en la cola
    sem_post(&cola_sem);

    free(temp);

    pthread_mutex_unlock(&cola_mutex);
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
// Funciones para manejar listas
// --------------------

ConsumerGroup *initConsumerGroup() {
    ConsumerGroup *list = malloc(sizeof(ConsumerGroup));
    list->count = 0;
    list->capacity = 5; // Máximo de 5 consumidores por grupo
    list->consumer_index = 0;
    list->consumers = malloc(sizeof(Consumer *) * list->capacity);
    return list;
}

void addConsumer(ConsumerGroup *list, Consumer *consumer) {
    if (list->count == list->capacity) {
        printf("El grupo ya tiene el máximo de consumidores (5).\n");
        return;
    }
    list->consumers[list->count++] = consumer;
}

ConsumerGroupContainer *initConsumerGroupContainer() {
    ConsumerGroupContainer *container = malloc(sizeof(ConsumerGroupContainer));
    container->head = NULL;
    pthread_mutex_init(&container->mutex, NULL);
    return container;
}

void addConsumerGroup(ConsumerGroupContainer *container, ConsumerGroup *group) {
    pthread_mutex_lock(&container->mutex);

    ConsumerGroupNode *newNode = malloc(sizeof(ConsumerGroupNode));
    newNode->group = group;
    newNode->next = container->head;
    container->head = newNode;

    pthread_mutex_unlock(&container->mutex);
}

// --------------------
// Funciones de manejo de mensajes
// --------------------

void printQueue(Queue *queue) {
    pthread_mutex_lock(&cola_mutex);
    printf("\n--- Mensajes en la Cola ---\n");
    Node *current = queue->front;
    while (current) {
        Message *msg = (Message *)current->data;
        printf("ID: %d | Origen: %s | Contenido: %s\n", msg->id, msg->origen, msg->mensaje);
        current = current->next;
    }
    printf("---------------------------\n");
    pthread_mutex_unlock(&cola_mutex);
}

//============================================================================

void deleteConsumer(ConsumerGroupContainer *container, int consumer_id) {
    pthread_mutex_lock(&container->mutex);

    ConsumerGroupNode *prevNode = NULL;
    ConsumerGroupNode *currentNode = container->head;

    while (currentNode) {
        ConsumerGroup *group = currentNode->group;

        for (int i = 0; i < group->count; i++) {
            if (group->consumers[i]->id == consumer_id) {
                // Eliminar el consumidor
                close(group->consumers[i]->socket_fd);
                free(group->consumers[i]);

                for (int j = i; j < group->count - 1; j++) {
                    group->consumers[j] = group->consumers[j + 1];
                }
                group->count--;

                // Si el grupo queda vacío, eliminarlo
                if (group->count == 0) {
                    free(group->consumers);
                    free(group);

                    if (prevNode) {
                        prevNode->next = currentNode->next;
                    } else {
                        container->head = currentNode->next;
                    }
                    free(currentNode);
                }

                pthread_mutex_unlock(&container->mutex);
                return;
            }
        }

        prevNode = currentNode;
        currentNode = currentNode->next;
    }

    pthread_mutex_unlock(&container->mutex);
}

//============================================================================

void sendMessageConsumers(Message *msg) {
    pthread_mutex_lock(&consumer_mutex);

    ConsumerGroupNode *currentNode = consumerGroups->head;
    while (currentNode) {
        ConsumerGroup *group = currentNode->group;

        if (group->count > 0) {
            // Seleccionar un consumidor aleatorio
            if(group->consumer_index >= group->count) {
                group->consumer_index = 0;
            }
            Consumer *consumer = group->consumers[group->consumer_index++];

            if (send(consumer->socket_fd, msg, sizeof(Message), 0) < 0) {
                perror("Error al enviar el mensaje al consumer");
                deleteConsumer(consumerGroups, consumer->id);
            } else {
                printf("Mensaje enviado al Consumer ID: %d\n", consumer->id);
            }
        }

        currentNode = currentNode->next;
    }

    pthread_mutex_unlock(&consumer_mutex);
}
// --------------------
// Manejo de conexiones
// --------------------

void *handlerConnProducer(void *arg) {
    int socket_cliente = *(int *)arg;
    free(arg);

    Message msg;
    ssize_t bytes_recibidos = recv(socket_cliente, &msg, sizeof(Message), 0);

    if (bytes_recibidos > 0) {
        msg.id = mensaje_id++;

        Message *msg_ptr = malloc(sizeof(Message));
        if (msg_ptr) {
            memcpy(msg_ptr, &msg, sizeof(Message));
            enqueue(cola, msg_ptr);

            printf("\nMensaje recibido de Producer:\n");
            printf("  ID: %d\n  Origen: %s\n  Contenido: %s\n", msg.id, msg.origen, msg.mensaje);

            pthread_cond_signal(&cola_cond); //Avisar al hilo que envia mensajes que hay un nuevo mensaje
            printQueue(cola);//Sola para pruebas
        } else {
            perror("No se pudo asignar memoria para el mensaje");
        }
    } else {
        perror("Error al recibir mensaje o conexión cerrada");
    }

    close(socket_cliente);
    pthread_exit(NULL);
}

void *handlerSendMessage(void *arg) {
    while (1) {
        pthread_mutex_lock(&cola_mutex);

        // Esperar a que haya mensajes en la cola
        while (isEmpty(cola)) {
            printf("Esperando mensajes en la cola...\n");
            pthread_cond_wait(&cola_cond, &cola_mutex);
        }

        pthread_mutex_unlock(&cola_mutex);

        // Verificar si hay consumidores disponibles
        pthread_mutex_lock(&consumer_mutex);
        int consumers_available = 0;
        ConsumerGroupNode *currentNode = consumerGroups->head;
        while (currentNode) {
            if (currentNode->group->count > 0) {
                consumers_available = 1;
                break;
            }
            currentNode = currentNode->next;
        }
        pthread_mutex_unlock(&consumer_mutex);

        if (!consumers_available) {
            printf("No hay consumidores disponibles. Esperando...\n");
            sleep(1); 
            continue;
        }

        // Si hay consumidores, procesar el mensaje
        Message *msg = (Message *)dequeue(cola);

        if (msg) {
            sendMessageConsumers(msg);
            free(msg);
        }
    }
    return NULL;
}

void *handlerConnConsumer(void *arg) {
    int socket_cliente = *(int *)arg;
    free(arg);

    Consumer *consumer = malloc(sizeof(Consumer));
    consumer->id = consumer_id++;
    consumer->socket_fd = socket_cliente;

    pthread_mutex_lock(&consumer_mutex);

    // Buscar un grupo con espacio disponible o crear uno nuevo
    ConsumerGroupNode *currentNode = consumerGroups->head;
    ConsumerGroup *targetGroup = NULL;

    while (currentNode) {
        if (currentNode->group->count < currentNode->group->capacity) {
            targetGroup = currentNode->group;
            break;
        }
        currentNode = currentNode->next;
    }

    if (!targetGroup) {
        // Crear un nuevo grupo si no hay espacio en los existentes
        targetGroup = initConsumerGroup();
        addConsumerGroup(consumerGroups, targetGroup);
    }

    addConsumer(targetGroup, consumer);
    pthread_mutex_unlock(&consumer_mutex);

    // Notificar a los hilos de envío que hay un nuevo consumidor
    pthread_cond_broadcast(&cola_cond);

    printf("Nuevo Consumer conectado con ID: %d\n", consumer->id);
    pthread_exit(NULL);
}

// --------------------
// Hilos para manejar conexiones
// --------------------

void *handlerThreadProducer(void *arg) {
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
        if (pthread_create(&hilo, NULL, handlerConnProducer, socket_ptr) != 0) {
            perror("No se pudo crear hilo para manejar conexión de Producer");
            close(nueva_conexion);
            free(socket_ptr);
        } else {
            pthread_detach(hilo);
        }
    }
}

void *handlerThreadConsumer(void *arg) {
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
        if (pthread_create(&hilo, NULL, handlerConnConsumer, socket_ptr) != 0) {
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

void init_broker() {
    int socket_producers, socket_consumers;
    struct sockaddr_in direccion_producers, direccion_consumers;

    cola = initQueue();
    sem_init(&cola_sem, 0, MAX_QUEUE_SIZE);
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

    pthread_t hilo_producers, hilo_consumers, hilo_enviar;
    pthread_create(&hilo_producers, NULL, handlerThreadProducer, &socket_producers);
    pthread_create(&hilo_consumers, NULL, handlerThreadConsumer, &socket_consumers);
    pthread_create(&hilo_enviar, NULL, handlerSendMessage, NULL);

    pthread_join(hilo_producers, NULL);
    pthread_join(hilo_consumers, NULL);
    pthread_join(hilo_enviar, NULL);

    freeQueue(cola);
    sem_destroy(&cola_sem);
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
            init_broker();
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