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
#include <sys/time.h>  // Para struct timeval
#include <signal.h>    // Para manejo de señales


// -------------------------
// Configuraciones
// -------------------------
#define MAX_FILENAME_LEN   256
#define MAX_TMPFILE_LEN    (MAX_FILENAME_LEN + 4)
#define QUEUE_CAPACITY     100
#define SOCKET_TIMEOUT_SEC 2

// -------------------------
// Estructuras de Datos
// -------------------------
typedef struct {
    long offset;
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
    long current_offset;
    pthread_mutex_t offset_mutex;
    pthread_mutex_t shutdown_mutex;
    int shutdown_flag;
} ConsumerContext;

// Variable global para manejo de señales
static ConsumerContext *global_ctx = NULL;

// -------------------------
// Funciones de Utilidad
// -------------------------
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

// -------------------------
// Funciones de la Cola
// -------------------------
Queue *initQueue() {    
    Queue *queue = malloc(sizeof(Queue));    
    queue->front = NULL;    
    queue->rear = NULL;    
    queue->size = 0;    
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond, NULL);    
    return queue;
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
    while (queue->front == NULL && !global_ctx->shutdown_flag) {        
        pthread_cond_wait(&queue->cond, &queue->mutex);    
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

// -------------------------
// Funciones de Persistencia
// -------------------------
long cargar_offset() {
    const char* filename = "offset.log";
    
    FILE* file = fopen(filename, "r");
    long offset = 0;
    if (file) {
        fscanf(file, "%ld", &offset);
        fclose(file);
    }
    return offset;
}

void guardar_offset(long offset) {
    const char* filename = "offset.log";
    const char* tmpfile = "offset.tmp";

    FILE* file = fopen(tmpfile, "w");
    if (!file) {
        perror("Error creando archivo temporal");
        return;
    }

    fprintf(file, "%ld", offset);
    fclose(file);
    
    if (rename(tmpfile, filename) != 0) {
        perror("Error renombrando archivo");
        remove(tmpfile);
    }
}

#include <poll.h>

// -------------------------
// Hilos de Trabajo (Versión mejorada con poll)
// -------------------------
void *receiver_thread(void *arg) {
    ConsumerContext *ctx = (ConsumerContext *)arg;
    Message msg;
    struct pollfd fds[1];   // poll para un socket
    int timeout_ms = 1000; // 1 segundo

    fds[0].fd = ctx->socket_cliente;
    fds[0].events = POLLIN;

    while (1) {
        // Verificar shutdown sin bloquear
        pthread_mutex_lock(&ctx->shutdown_mutex);
        if (ctx->shutdown_flag) {
            pthread_mutex_unlock(&ctx->shutdown_mutex);
            break;
        }
        pthread_mutex_unlock(&ctx->shutdown_mutex);

        int ret = poll(fds, 1, timeout_ms);
        
        if (ret > 0) {
            if (fds[0].revents & POLLIN) {
                // Procesar datos
                ssize_t bytes;
                while ((bytes = recv(ctx->socket_cliente, &msg, sizeof(Message), 0))) {
                    if (bytes > 0) {
                        Message *msg_copy = malloc(sizeof(Message));
                        memcpy(msg_copy, &msg, sizeof(Message));
                        enqueue(ctx->cola, msg_copy);
                        printf("[Receptor] Mensaje recibido - Offset: %ld\n", msg.offset);
                    } else if (bytes == 0) {
                        printf("Conexión cerrada por el broker\n");
                        goto cleanup;
                    } else {
                        if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            perror("Error en recepción");
                            goto cleanup;
                        }
                        break;
                    }
                }
            }
        } else if (ret < 0) {
            perror("Error en poll()");
            break;
        }
        // Timeout: volver a verificar shutdown
    }

    cleanup:
    close(ctx->socket_cliente);
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

        printf("\n[Offset: %ld] Origen: %s\nMessage: %s\n", 
              msg->offset, msg->origen, msg->message);
            
        pthread_mutex_lock(&ctx->offset_mutex);
        ctx->current_offset = msg->offset + 1;
        guardar_offset(ctx->current_offset);
        
        char ack_msg[64];
        snprintf(ack_msg, sizeof(ack_msg), "ACK=%ld", msg->offset);
        send(ctx->socket_cliente, ack_msg, strlen(ack_msg), 0);
        pthread_mutex_unlock(&ctx->offset_mutex);
        
        free(msg);
    }
    printf("Hilo procesador finalizado\n");
    return NULL;
}

// -------------------------
// Manejo de Señales
// -------------------------
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

// -------------------------
// Configuración Principal
// -------------------------
int main(int argc, char *argv[]) {
    ConsumerContext ctx = {
        .current_offset = cargar_offset(),
        .shutdown_flag = 0
    };
    
    ctx.cola = initQueue();
    pthread_mutex_init(&ctx.offset_mutex, NULL);
    pthread_mutex_init(&ctx.shutdown_mutex, NULL);

    // Configuración del socket
    ctx.socket_cliente = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in broker_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(8000),
        .sin_addr.s_addr = INADDR_ANY
    };
    
    if (connect(ctx.socket_cliente, (struct sockaddr*)&broker_addr, sizeof(broker_addr)) < 0) {
        handle_error("Error de conexión", 1);
    }

    set_nonblocking(ctx.socket_cliente);
    set_socket_timeout(ctx.socket_cliente);

    // Registro inicial
    char init_msg[64];
    snprintf(init_msg, sizeof(init_msg), "OFFSET=%ld", ctx.current_offset);
    send(ctx.socket_cliente, init_msg, strlen(init_msg), 0);

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
    Node *current = ctx.cola->front;
    while (current) {
        Node *next = current->next;
        free(current->data);
        free(current);
        current = next;
    }
    free(ctx.cola);
    close(ctx.socket_cliente);
    pthread_mutex_destroy(&ctx.offset_mutex);
    pthread_mutex_destroy(&ctx.shutdown_mutex);

    printf("Consumer detenido correctamente\n");
    return EXIT_SUCCESS;
}