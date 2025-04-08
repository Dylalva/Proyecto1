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
// Estructura de mensaje
// --------------------

typedef struct {
    int id;
    char origen[50];
    char mensaje[256];
} Mensaje;

// --------------------
// Cola global y contador
// --------------------

Queue *cola;
int mensaje_id = 0;
pthread_mutex_t cola_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// --------------------
// Manejo de conexión
// --------------------

void logMensaje(Mensaje *msg) {
    pthread_mutex_lock(&log_mutex);

    FILE *log = fopen("log.txt", "a");
    if (log == NULL) {
        perror("Error al abrir log.txt");
        pthread_mutex_unlock(&log_mutex);
        return;
    }

    fprintf(log, "ID: %d | Origen: %s | Contenido: %s\n", msg->id, msg->origen, msg->mensaje);
    fclose(log);

    pthread_mutex_unlock(&log_mutex);
}

void *manejarConexion(void *arg) {
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

            printf("\nMensaje recibido:\n");
            printf("  ID: %d\n  Origen: %s\n  Contenido: %s\n", msg.id, msg.origen, msg.mensaje);

            // guardar el mensaje en el archivo log
            logMensaje(msg_ptr);
            
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

// --------------------
// Función principal del broker
// --------------------

void iniciar_broker() {
    int socket_servidor;
    struct sockaddr_in direccion_servidor, direccion_cliente;
    socklen_t tam_cliente = sizeof(direccion_cliente);

    cola = initQueue();

    socket_servidor = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_servidor < 0) {
        perror("Error al crear el socket del servidor");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(socket_servidor, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    direccion_servidor.sin_family = AF_INET;
    direccion_servidor.sin_port = htons(8080);
    direccion_servidor.sin_addr.s_addr = INADDR_ANY;

    if (bind(socket_servidor, (struct sockaddr *)&direccion_servidor, sizeof(direccion_servidor)) < 0) {
        perror("Error al hacer bind");
        close(socket_servidor);
        exit(EXIT_FAILURE);
    }

    if (listen(socket_servidor, 1000) < 0) {
        perror("Error al escuchar");
        close(socket_servidor);
        exit(EXIT_FAILURE);
    }

    printf("Broker ACTIVO escuchando en el puerto 8080...\n");

    while (1) {
        int nueva_conexion = accept(socket_servidor, (struct sockaddr *)&direccion_cliente, &tam_cliente);
        if (nueva_conexion < 0) {
            perror("Error al aceptar conexión");
            continue;
        }

        int *socket_ptr = malloc(sizeof(int));
        *socket_ptr = nueva_conexion;

        pthread_t hilo;
        if (pthread_create(&hilo, NULL, manejarConexion, socket_ptr) != 0) {
            perror("No se pudo crear hilo para manejar conexión");
            close(nueva_conexion);
            free(socket_ptr);
        } else {
            pthread_detach(hilo);
        }
    }

    freeQueue(cola);
    close(socket_servidor);
}

// --------------------
// Mecanismo de failover
// --------------------

int main() {
    int intento = 0;

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
