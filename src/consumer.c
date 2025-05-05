//Constante para el manejo de señales
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <sys/time.h>
#include <signal.h>

// -------------------------
// Definiciones de constantes
// -------------------------
#define MAX_FILENAME_LEN   256
#define MAX_TMPFILE_LEN    (MAX_FILENAME_LEN + 4)
#define QUEUE_CAPACITY     100
#define SOCKET_TIMEOUT_SEC 2
#define RECONNECT_TIMEOUT  30 // Tiempo máximo para intentar reconexión (en segundos)

// -------------------------
// Estructuras de Datos
// -------------------------
typedef enum { 
    MSG_DATA = 0, 
    MSG_ACK = 1 } 
    MessageType;

typedef struct {
    MessageType type;
    uint32_t    offset;
    int id;
    char origen[50];
    char message[256];
} Message;

typedef struct Node {    
    void *data;    
    struct Node *next;    
} Node;

typedef struct Queue {    
    Node *front;    
    Node *rear;    
    int size;    
    pthread_mutex_t mutex;
    pthread_cond_t cond;  
} Queue;

typedef struct {
    int socket_cliente;
    Queue *cola;
    pthread_mutex_t shutdown_mutex;
    pthread_mutex_t socket_mutex;
    int shutdown_flag;
} ConsumerContext;

// -------------------------
// Declaraciones de funciones
// -------------------------

// Funciones de utilidad
void handle_error(const char *msg, int fatal);
void set_nonblocking(int sockfd);
void set_socket_timeout(int sockfd);
int reconnect_to_broker(ConsumerContext *ctx, struct sockaddr_in *broker_addr);

// Funciones de la cola
Queue *initQueue();
void freeQueue(Queue *queue);
void enqueue(Queue *queue, void *data);
void *dequeue(Queue *queue);

// Hilos de trabajo
void *receiver_thread(void *arg);
void *processor_thread(void *arg);

// Manejo de señales
void shutdown_handler(int sig);
void setup_signal_handlers();

// -------------------------
// Variable global
// -------------------------
static ConsumerContext *global_ctx = NULL;

// -------------------------
// Función principal
// -------------------------
int main(int argc, char *argv[]) {
    ConsumerContext ctx = {
        .shutdown_flag = 0
    };
    
    ctx.cola = initQueue();
    pthread_mutex_init(&ctx.shutdown_mutex, NULL);
    pthread_mutex_init(&ctx.socket_mutex, NULL);

    // Configuración del socket
    struct sockaddr_in broker_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(8082),
        .sin_addr.s_addr = INADDR_ANY
    };

    time_t start_time = time(NULL);
    while (1) {
        ctx.socket_cliente = socket(AF_INET, SOCK_STREAM, 0);
        if (ctx.socket_cliente < 0) {
            perror("Error al crear el socket");
            sleep(1);
            continue;
        }

        if (connect(ctx.socket_cliente, (struct sockaddr*)&broker_addr, sizeof(broker_addr)) == 0) {
            printf("Conexión inicial establecida con el broker.\n");
            printf("Consumer conectado exitosamente al broker en el puerto 8082.\n");
            break; // Salir del bucle si la conexión es exitosa
        }

        perror("Error al conectar con el broker. Reintentando...");
        close(ctx.socket_cliente);

        if (time(NULL) - start_time >= RECONNECT_TIMEOUT) {
            fprintf(stderr, "No se pudo conectar con el broker después de %d segundos. Terminando el proceso.\n", RECONNECT_TIMEOUT);
            exit(EXIT_FAILURE);
        }

        sleep(1); // Esperar 1 segundo antes de reintentar
    }

    set_nonblocking(ctx.socket_cliente);
    set_socket_timeout(ctx.socket_cliente);

    // Configurar señales
    global_ctx = &ctx;
    setup_signal_handlers();

    // Iniciar hilos
    pthread_t threads[2];
    pthread_create(&threads[0], NULL, receiver_thread, &ctx);
    pthread_create(&threads[1], NULL, processor_thread, &ctx);

    // Esperar finalización
    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);

    // Limpieza final
    printf("Realizando limpieza final...\n");
    freeQueue(ctx.cola);
    close(ctx.socket_cliente);
    pthread_mutex_destroy(&ctx.shutdown_mutex);
    pthread_mutex_destroy(&ctx.socket_mutex);

    printf("Consumer detenido correctamente\n");
    return EXIT_SUCCESS;
}

// -------------------------
// Definiciones de funciones
// -------------------------

// Funciones de utilidad
void handle_error(const char *msg, int fatal) {
    perror(msg);
    if (fatal) exit(EXIT_FAILURE);
}

void set_nonblocking(int sockfd) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        handle_error("Error setting non-blocking", 1);
    }
}

void set_socket_timeout(int sockfd) {
    struct timeval tv = {.tv_sec = SOCKET_TIMEOUT_SEC, .tv_usec = 0};
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

int reconnect_to_broker(ConsumerContext *ctx, struct sockaddr_in *broker_addr) {
    time_t start_time = time(NULL);
    while (time(NULL) - start_time < RECONNECT_TIMEOUT) {
        printf("Intentando reconectar con el broker...\n");

        int new_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (new_socket < 0) {
            perror("Error al crear el socket");
            sleep(1);
            continue;
        }

        if (connect(new_socket, (struct sockaddr *)broker_addr, sizeof(*broker_addr)) == 0) {
            printf("Reconexión exitosa con el broker.\n");

            pthread_mutex_lock(&ctx->socket_mutex);
            if (ctx->socket_cliente != -1) {
                close(ctx->socket_cliente);
            }
            ctx->socket_cliente = new_socket;
            pthread_mutex_unlock(&ctx->socket_mutex);

            set_nonblocking(ctx->socket_cliente);
            set_socket_timeout(ctx->socket_cliente);
            return 1; // Reconexión exitosa
        }

        perror("Error al reconectar con el broker");
        close(new_socket);
        sleep(1);
    }

    printf("No se pudo reconectar con el broker después de %d segundos.\n", RECONNECT_TIMEOUT);
    return 0; // Reconexión fallida
}

// Funciones de la cola
Queue *initQueue() {    
    Queue *queue = malloc(sizeof(Queue));    
    queue->front = NULL;    
    queue->rear = NULL;    
    queue->size = 0;    
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond, NULL);    
    return queue;
}

void freeQueue(Queue *queue) {    
    Node *current = queue->front;    
    while (current) {        
        Node *next = current->next;        
        free(current->data);        
        free(current);        
        current = next;    
    }    
    free(queue);    
}

void enqueue(Queue *queue, void *data) {    
    pthread_mutex_lock(&queue->mutex);    
    Node *newNode = malloc(sizeof(Node));    
    newNode->data = data;    
    newNode->next = NULL;    
    
    if (queue->rear == NULL) {        
        queue->front = newNode;       
    } else {        
        queue->rear->next = newNode;        
    }    
    queue->rear = newNode;    
    queue->size++;
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
}

void *dequeue(Queue *queue) {    
    pthread_mutex_lock(&queue->mutex);  
    
    // Atomicidad de la variable de cierre
    pthread_mutex_lock(&global_ctx->shutdown_mutex);
    int shutdown = global_ctx->shutdown_flag;
    pthread_mutex_unlock(&global_ctx->shutdown_mutex);

    while (queue->front == NULL && !shutdown) {        
        pthread_cond_wait(&queue->cond, &queue->mutex);   

        // Actualizar shutdown dentro del bucle
        pthread_mutex_lock(&global_ctx->shutdown_mutex);
        shutdown = global_ctx->shutdown_flag;
        pthread_mutex_unlock(&global_ctx->shutdown_mutex);
    }    
    
    if (queue->front == NULL) {        
        pthread_mutex_unlock(&queue->mutex);    
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
    pthread_mutex_unlock(&queue->mutex);    
    return data;
}

void *receiver_thread(void *arg) {
    ConsumerContext *ctx = (ConsumerContext *)arg;
    Message msg;
    struct pollfd fds[1];
    int timeout_ms = 1000;
    struct sockaddr_in broker_addr = { 
        .sin_family = AF_INET,
        .sin_port = htons(8082),
        .sin_addr.s_addr = INADDR_ANY };

    while (1) {
        pthread_mutex_lock(&ctx->shutdown_mutex);
        if (ctx->shutdown_flag) {
            pthread_mutex_unlock(&ctx->shutdown_mutex);
            break;
        }
        pthread_mutex_unlock(&ctx->shutdown_mutex);
        
        pthread_mutex_lock(&ctx->socket_mutex);
        fds[0].fd = ctx->socket_cliente;
        pthread_mutex_unlock(&ctx->socket_mutex);
        fds[0].events = POLLIN;

        int ret = poll(fds, 1, timeout_ms);
        if (ret > 0 && (fds[0].revents & POLLIN)) {
            ssize_t bytes = recv(fds[0].fd, &msg, sizeof(Message), 0);
            if (bytes == sizeof(Message) && msg.type == MSG_DATA) {
                // procesar datos
                Message *msg_copy = malloc(sizeof(Message));
                memcpy(msg_copy, &msg, sizeof(Message));
                enqueue(ctx->cola, msg_copy);
                printf("[Receptor] Mensaje recibido - Offset: %u\n", msg.offset);

                // enviar ACK como Message
                Message ack;
                ack.type   = MSG_ACK;
                ack.offset = msg.offset;
                size_t to_send = sizeof(ack);
                uint8_t *buf = (uint8_t*)&ack;
                while (to_send > 0) {
                    ssize_t sent = send(fds[0].fd, buf, to_send, MSG_NOSIGNAL);
                    if (sent <= 0) {
                        perror("Error enviando ACK");
                        break;
                    }
                    buf     += sent;
                    to_send -= sent;
                }
            }
            else if (bytes == 0) {
                printf("Conexión cerrada por el broker. Intentando reconexión…\n");
                if (!reconnect_to_broker(ctx, &broker_addr)) break;
            }
            else {
                perror("Error en recepción");
            }
        }
        else if (ret < 0) {
            perror("Error en poll()");
            break;
        }
    }

    close(fds[0].fd);
    printf("Hilo receptor finalizado\n");
    return NULL;
}


void *processor_thread(void *arg) {
    ConsumerContext *ctx = (ConsumerContext *)arg;
    
    while (1) {
        pthread_mutex_lock(&ctx->shutdown_mutex);
        if (ctx->shutdown_flag) {
            pthread_mutex_unlock(&ctx->shutdown_mutex);
            break;
        }
        pthread_mutex_unlock(&ctx->shutdown_mutex);
        
        Message *msg = dequeue(ctx->cola);
        if (!msg) continue;

        printf("\n[Offset: %u] Origen: %s\nMessage: %s\n", 
              msg->offset, msg->origen, msg->message);
            
        free(msg);
    }
    printf("Hilo procesador finalizado\n");
    return NULL;
}

// Manejo de señales
void shutdown_handler(int sig) {
    printf("\nRecibida señal %d. Iniciando cierre seguro...\n", sig);
    
    if (global_ctx) {
        pthread_mutex_lock(&global_ctx->shutdown_mutex);
        global_ctx->shutdown_flag = 1;
        pthread_mutex_unlock(&global_ctx->shutdown_mutex);
        
        // Despertar todos los hilos bloqueados
        pthread_mutex_lock(&global_ctx->cola->mutex);
        pthread_cond_broadcast(&global_ctx->cola->cond);
        pthread_mutex_unlock(&global_ctx->cola->mutex);
    }
}

void setup_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = shutdown_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    
    sigaction(SIGINT, &sa, NULL);   // Ctrl+C
    sigaction(SIGTERM, &sa, NULL);  // kill
}