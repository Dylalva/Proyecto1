#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/wait.h> // Para waitpid
#include <time.h>

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

typedef enum {
    MSG_DATA = 0,  // paquete de datos
    MSG_ACK  = 1   // confirmación (ACK)
} MessageType;

typedef struct {
    MessageType type;   // indica si es MSG_DATA o MSG_ACK
    uint32_t    offset; 
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
    long offset_group;
    int count;
    int capacity;
    int consumer_index;
    pthread_t thread_group;
    pthread_mutex_t group_mutex; // Mutex para proteger el acceso al grupo
    pthread_cond_t consumer_available;
} ConsumerGroup;

typedef struct ConsumerGroupNode {
    ConsumerGroup *group;
    struct ConsumerGroupNode *next;
} ConsumerGroupNode;

typedef struct {
    ConsumerGroupNode *head;
    pthread_mutex_t mutex;
} ConsumerGroupContainer;

typedef struct {
    Consumer *consumer;
    Message *msg;
} ConsumerMessageArgs;

// --------------------
// Variables globales
// --------------------

Queue *cola;
int mensaje_id = 0;
int consumer_id = 0;
pthread_mutex_t mensaje_id_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t consumer_id_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t cola_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cola_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t consumer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t create_mutex = PTHREAD_MUTEX_INITIALIZER;

#define MAX_QUEUE_SIZE 1000 // Límite máximo de mensajes en la cola
sem_t cola_sem; // Semáforo para controlar el tamaño de la cola
ConsumerGroupContainer *consumerGroups;

// --------------------
// Declaraciones de funciones
// --------------------

// Funciones de la cola
void sigchld_handler(int sig); // Declaración de la función sigchld_handler
Queue *initQueue();
void enqueue(Queue *queue, void *data);
void *dequeue(Queue *queue);
int isEmpty(Queue *queue);
void freeQueue(Queue *queue);

// Funciones para manejar listas
ConsumerGroup *initConsumerGroup();
void addConsumer(ConsumerGroup *list, Consumer *consumer);
ConsumerGroupContainer *initConsumerGroupContainer();
void addConsumerGroup(ConsumerGroupContainer *container, ConsumerGroup *group);
void deleteConsumer(ConsumerGroupContainer *container, int consumer_id);

// Funciones de manejo de mensajes
void printQueue(Queue *queue);
void printConsumers();
void logMessageToFile(int consumer_id, Message *msg);
void sendMessageConsumers(Message *msg);
void *resendMessageToConsumer(void *arg);

// Funciones de manejo de conexiones
void *handlerConnProducer(void *arg);
void *handlerSendMessage(void *arg);
void *handlerConnConsumer(void *arg);
void *handlerThreadProducer(void *arg);
void *handlerThreadConsumer(void *arg);

// Función principal del broker
void init_broker();

// Mecanismo de failover
int is_broker_active();

// --------------------
// Función principal
// --------------------

int main() {
    signal(SIGPIPE, SIG_IGN); // Ignorar señales SIGPIPE
    signal(SIGCHLD, sigchld_handler); // Manejar la señal SIGCHLD

    pid_t broker_pid = -1; // PID del proceso broker

    while (1) {
        if (!is_broker_active()) {
            if (broker_pid == -1) { // Solo crear un nuevo broker si no hay uno en ejecución
                printf("No hay broker activo. Iniciando uno nuevo...\n");

                broker_pid = fork(); // Crear un proceso hijo
                if (broker_pid < 0) {
                    perror("Error al crear el proceso broker");
                    exit(EXIT_FAILURE);
                }

                if (broker_pid == 0) {
                    // Proceso hijo: ejecutar el broker
                    init_broker();
                    exit(EXIT_SUCCESS); // Salir cuando el broker termine
                } else {
                    // Proceso padre: continuar como monitor
                    printf("Broker iniciado con PID: %d\n", broker_pid);
                }
            }
        } else {
            printf("Broker ya está activo. Verificando nuevamente en 5 segundos...\n");
        }

        // Esperar 5 segundos antes de verificar nuevamente
        sleep(5);

        // Verificar si el proceso broker sigue activo
        if (broker_pid > 0 && kill(broker_pid, 0) != 0) {
            printf("El proceso broker con PID %d ha terminado.\n", broker_pid);
            broker_pid = -1; // Resetear el PID del broker
        }
    }

    return 0;
}

// --------------------
// Definiciones de funciones
// --------------------

// Funciones de la cola
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

// Funciones para manejar listas
ConsumerGroup *initConsumerGroup() {
    ConsumerGroup *list = malloc(sizeof(ConsumerGroup));
    list->offset_group = 0;
    list->count = 0;
    list->capacity = 5; // Máximo de 5 consumidores por grupo
    list->consumer_index = 0;
    list->consumers = malloc(sizeof(Consumer *) * list->capacity);
    pthread_mutex_init(&list->group_mutex, NULL);
    pthread_cond_init(&list->consumer_available, NULL);

    return list;
}

void addConsumer(ConsumerGroup *list, Consumer *consumer) {
    pthread_mutex_lock(&list->group_mutex);
    
    // Verificar si el grupo está lleno
    if (list->count == list->capacity) {
        printf("El grupo ya tiene el máximo de consumidores (5).\n");
        pthread_mutex_unlock(&list->group_mutex);
        return;
    }

    // Agregar el nuevo consumidor
    list->consumers[list->count++] = consumer;
    
    // Si hay mensajes pendientes en la cola, notificar a todos los consumidores
    if (list->count == 1) {
        // Si es el primer consumidor, señalar que hay un consumidor disponible
        pthread_cond_broadcast(&list->consumer_available);
    }

    pthread_mutex_unlock(&list->group_mutex);
}

ConsumerGroupContainer *initConsumerGroupContainer() {
    ConsumerGroupContainer *container = malloc(sizeof(ConsumerGroupContainer));
    container->head = NULL;
    pthread_mutex_init(&container->mutex, NULL);
    return container;
}

void addConsumerGroup(ConsumerGroupContainer *container, ConsumerGroup *group) {
    ConsumerGroupNode *newNode = malloc(sizeof(ConsumerGroupNode));
    newNode->group = group;
    newNode->next = container->head;
    container->head = newNode;
}

void deleteConsumer(ConsumerGroupContainer *container, int consumer_id) {
    ConsumerGroupNode *node = container->head;
    while (node) {
        ConsumerGroup *group = node->group;
        for (int i = 0; i < group->count; i++) {
            if (group->consumers[i]->id == consumer_id) {
                // Cerrar y liberar el consumidor
                close(group->consumers[i]->socket_fd);
                free(group->consumers[i]);
                // Reorganizar el array
                for (int j = i; j < group->count - 1; j++)
                    group->consumers[j] = group->consumers[j + 1];
                group->count--;

                // Ajustar índice si hace falta
                if (group->consumer_index >= group->count)
                    group->consumer_index = 0;

                // Notificar a cualquier hilo esperando
                pthread_cond_broadcast(&group->consumer_available);

                // El grupo se mantiene creado por si algun mensaje queda pendiente
                return;
            }
        }
        node = node->next;
    }
}


// Funciones de manejo de mensajes
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

void printConsumers() {
    ConsumerGroupNode *node = consumerGroups->head;
    int cont = 0;
    while (node) {
        ConsumerGroup *group = node->group;
        printf("\n--- Grupo de Consumidores : %d ---\n", cont++);
        for (int i = 0; i < group->count; i++) {
            printf("Consumer ID: %d | Socket FD: %d\n", group->consumers[i]->id, group->consumers[i]->socket_fd);
        }
        node = node->next;
    }
}

// Log de mensajes con consumers
void logMessageToFile(int consumer_id, Message *msg) {
    pthread_mutex_lock(&log_mutex);

    FILE *log_file = fopen("consumer.log", "a"); // Abrir el archivo en modo append
    if (log_file) {
        fprintf(log_file, "Consumer ID: %d | Mensaje ID: %u \n",
                consumer_id, msg->offset);
        fclose(log_file);
    } else {
        perror("Error al abrir el archivo consumer.log");
    }

    pthread_mutex_unlock(&log_mutex);
}

void sendMessageConsumers(Message *original_msg) {
    // Copiar mensaje una sola vez
    Message *msg = malloc(sizeof(Message));
    memcpy(msg, original_msg, sizeof(Message));
    msg->type = MSG_DATA;

    int message_sent = 0;

    while (!message_sent) {
        // Acceder al único grupo o iterar si hay varios
        pthread_mutex_lock(&consumerGroups->mutex);
        ConsumerGroupNode *node = consumerGroups->head;
        pthread_mutex_unlock(&consumerGroups->mutex);

        // Asumimos un solo grupo; si hay varios, iterar aquí
        ConsumerGroup *group = node->group;

        // Esperar consumidores disponibles
        pthread_mutex_lock(&group->group_mutex);
        while (group->count == 0) {
            printf("Esperando a que se conecte un consumidor para offset %u...", msg->offset);
            pthread_cond_wait(&group->consumer_available, &group->group_mutex);
        }
        
        // Preparar offset actualizado
        msg->offset = group->offset_group;

        // Intentos por consumidor
        int attempts = 0;
        int total = group->count;
        int idx = group->consumer_index;

        // Intentar enviar hasta tener éxito
        while (!message_sent && attempts < total) {
            Consumer *consumer = group->consumers[idx];
            int fd = consumer->socket_fd;

            // Enviar mensaje completo
            uint8_t *buf = (uint8_t*)msg;
            size_t left = sizeof(Message);
            ssize_t sent;
            while (left > 0) {
                sent = send(fd, buf, left, MSG_NOSIGNAL);
                if (sent <= 0) break;
                buf += sent;
                left -= sent;
            }
            if (sent <= 0) {
                // Consumidor desconectado o error, eliminar y probar siguiente
                deleteConsumer(consumerGroups, consumer->id);
                pthread_mutex_unlock(&group->group_mutex);
                attempts++;
                idx = (idx + 1) % (group->count > 0 ? group->count : 1);
                pthread_mutex_lock(&group->group_mutex);
                continue;
            }

            // Esperar ACK
            fd_set rfds;
            struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
            FD_ZERO(&rfds);
            FD_SET(fd, &rfds);
            int r = select(fd+1, &rfds, NULL, NULL, &tv);
            if (r > 0 && FD_ISSET(fd, &rfds)) {
                Message ack;
                if (recv(fd, &ack, sizeof(ack), MSG_WAITALL) == sizeof(ack)
                    && ack.type == MSG_ACK && ack.offset == msg->offset) {
                    // Envío confirmado
                    logMessageToFile(consumer->id, msg);
                    group->offset_group++;
                    group->consumer_index = (idx + 1) % group->count;
                    message_sent = 1;
                } else {
                    // ACK no válido: elimino consumidor y sigo
                    deleteConsumer(consumerGroups, consumer->id);
                    pthread_mutex_unlock(&group->group_mutex);
                    attempts++;
                    idx = (idx + 1) % (group->count > 0 ? group->count : 1);
                    pthread_mutex_lock(&group->group_mutex);
                }
            } else {
                // Timeout o error: elimino y sigo
                if (r < 0) perror("select");
                deleteConsumer(consumerGroups, consumer->id);
                pthread_mutex_unlock(&group->group_mutex);
                attempts++;
                idx = (idx + 1) % (group->count > 0 ? group->count : 1);
                pthread_mutex_lock(&group->group_mutex);
            }
        }

        pthread_mutex_unlock(&group->group_mutex);
    }

    free(msg);
}

// Funciones de manejo de conexiones
void *handlerConnProducer(void *arg) {
    int socket_cliente = *(int *)arg;
    free(arg);

    Message msg;
    ssize_t bytes_recibidos = recv(socket_cliente, &msg, sizeof(Message), 0);

    if (bytes_recibidos > 0) {
        pthread_mutex_lock(&mensaje_id_mutex);
        msg.id = mensaje_id++;
        pthread_mutex_unlock(&mensaje_id_mutex);

        // Registrar el mensaje en el archivo mensajes.log
        pthread_mutex_lock(&log_mutex); // Bloquear el mutex antes de escribir en el archivo
        FILE *log_file = fopen("mensajes.log", "a");
        if (log_file) {
            fprintf(log_file, "ID: %d | Origen: %s | Contenido: %s\n", msg.id, msg.origen, msg.mensaje);
            fclose(log_file);
        } else {
            perror("Error al abrir el archivo mensajes.log");
        }
        pthread_mutex_unlock(&log_mutex); // Liberar el mutex después de escribir en el archivo

        Message *msg_ptr = malloc(sizeof(Message));
        if (msg_ptr) {
            memcpy(msg_ptr, &msg, sizeof(Message));
            enqueue(cola, msg_ptr);

            printf("\nMensaje recibido de Producer:\n");
            printf("  ID: %d\n  Origen: %s\n  Contenido: %s\n", msg.id, msg.origen, msg.mensaje);
            
            pthread_mutex_lock(&cola_mutex);
            pthread_cond_signal(&cola_cond); // Avisar al hilo que envía mensajes que hay un nuevo mensaje
            pthread_mutex_unlock(&cola_mutex);
            printQueue(cola); // Solo para pruebas
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
        pthread_mutex_lock(&consumerGroups->mutex);
        int consumers_available = 0;
        ConsumerGroupNode *currentNode = consumerGroups->head;
        while (currentNode) {
            if (currentNode->group->count > 0) {
                consumers_available = 1;
                break;
            }
            currentNode = currentNode->next;
        }
        pthread_mutex_unlock(&consumerGroups->mutex);

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
    pthread_mutex_lock(&consumerGroups->mutex);
    pthread_mutex_lock(&consumer_id_mutex);
    consumer->id = consumer_id++;
    pthread_mutex_unlock(&consumer_id_mutex);
    
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

    // Agregar consumidor al grupo
    addConsumer(targetGroup, consumer);


    // Notificar a los hilos de envío con el mutex correcto
    pthread_mutex_lock(&cola_mutex);
    pthread_cond_broadcast(&cola_cond);
    pthread_mutex_unlock(&cola_mutex);
 
    pthread_mutex_unlock(&consumer_mutex);
    pthread_mutex_unlock(&consumerGroups->mutex);

    printf("Nuevo Consumer conectado con ID: %d\n", consumer->id);
    pthread_exit(NULL);
}

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
        pthread_mutex_lock(&create_mutex);
        if (pthread_create(&hilo, NULL, handlerConnProducer, socket_ptr) != 0) {
            perror("No se pudo crear hilo para manejar conexión de Producer");
            close(nueva_conexion);
            free(socket_ptr);
        } else {
            pthread_detach(hilo);
        }
        pthread_mutex_unlock(&create_mutex);
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
        pthread_mutex_lock(&create_mutex);
        if (pthread_create(&hilo, NULL, handlerConnConsumer, socket_ptr) != 0) {
            perror("No se pudo crear hilo para manejar conexión de Consumer");
            close(nueva_conexion);
            free(socket_ptr);
        } else {
            pthread_detach(hilo);
        }
        pthread_mutex_unlock(&create_mutex);
    }
}

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
    pthread_mutex_lock(&create_mutex);
    pthread_create(&hilo_producers, NULL, handlerThreadProducer, &socket_producers);
    pthread_create(&hilo_consumers, NULL, handlerThreadConsumer, &socket_consumers);
    pthread_create(&hilo_enviar, NULL, handlerSendMessage, NULL);
    pthread_mutex_unlock(&create_mutex);

    pthread_join(hilo_producers, NULL);
    pthread_join(hilo_consumers, NULL);
    pthread_join(hilo_enviar, NULL);

    freeQueue(cola);
    sem_destroy(&cola_sem);
    close(socket_producers);
    close(socket_consumers);
}

int is_broker_active() {
    int socket_prueba = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_prueba < 0) {
        perror("Error creando socket de prueba");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in direccion;
    direccion.sin_family = AF_INET;
    direccion.sin_port = htons(8081); // Puerto de control del broker
    direccion.sin_addr.s_addr = INADDR_ANY;

    int result = bind(socket_prueba, (struct sockaddr *)&direccion, sizeof(direccion));
    close(socket_prueba);

    if (result == 0) {
        // El puerto está disponible, no hay broker activo
        return 0;
    } else if (errno == EADDRINUSE) {
        // El puerto está en uso, hay un broker activo
        return 1;
    } else {
        perror("Error inesperado al verificar el puerto");
        exit(EXIT_FAILURE);
    }
}

void sigchld_handler(int sig) {
    (void)sig; // Evitar advertencias por no usar el parámetro

    // Limpiar todos los procesos hijos terminados
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        // No hacer nada, solo limpiar los procesos hijos
    }
}