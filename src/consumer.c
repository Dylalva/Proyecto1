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
typedef enum { MSG_DATA = 0, MSG_ACK = 1 } MessageType;

typedef struct {
    MessageType type;
    uint32_t    offset;
    int         id;
    char        origen[50];
    char        message[256];
} Message;

typedef struct Node {
    void *data;
    struct Node *next;
} Node;

typedef struct Queue {
    Node *front;
    Node *rear;
    int   size;
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
} Queue;

typedef struct {
    int socket_cliente;
    Queue *cola;
    pthread_mutex_t shutdown_mutex;
    pthread_mutex_t socket_mutex;
    int shutdown_flag;
} ConsumerContext;

// Declaraciones de funciones
void set_nonblocking(int sockfd);
void set_socket_timeout(int sockfd);
int reconnect_to_broker(ConsumerContext *ctx, struct sockaddr_in *broker_addr);

Queue *initQueue();
void freeQueue(Queue *queue);
void enqueue(Queue *queue, void *data);
void *dequeue(Queue *queue);

void *receiver_thread(void *arg);
void *processor_thread(void *arg);

void shutdown_handler(int sig);
void setup_signal_handlers();


static ConsumerContext *global_ctx = NULL;

int main(int argc, char *argv[]) {
    ConsumerContext ctx;
    ctx.shutdown_flag = 0;
    ctx.cola = initQueue();
    pthread_mutex_init(&ctx.shutdown_mutex, NULL);
    pthread_mutex_init(&ctx.socket_mutex, NULL);

    // Configura direcciones de brokers (primario y failover)
    struct sockaddr_in brokers[2];
    memset(&brokers, 0, sizeof(brokers));
    // broker principal
    brokers[0].sin_family = AF_INET;
    brokers[0].sin_port = htons(8082);
    inet_pton(AF_INET, "127.0.0.1", &brokers[0].sin_addr);
    // failover
    brokers[1].sin_family = AF_INET;
    brokers[1].sin_port = htons(8083);
    inet_pton(AF_INET, "127.0.0.1", &brokers[1].sin_addr);

    int connected = 0;
    for (int i = 0; i < 2; i++) {
        ctx.socket_cliente = socket(AF_INET, SOCK_STREAM, 0);
        if (ctx.socket_cliente < 0) continue;
        if (connect(ctx.socket_cliente, (struct sockaddr*)&brokers[i], sizeof(brokers[i])) == 0) {
            printf("Se ha conectado al broker %d\n", i);
            connected = 1;
            break;
        }
        close(ctx.socket_cliente);
    }
    if (!connected) {
        fprintf(stderr, "No hay brokers disponibles. Saliendo.\n");
        exit(EXIT_FAILURE);
    }

    set_nonblocking(ctx.socket_cliente);
    set_socket_timeout(ctx.socket_cliente);

    global_ctx = &ctx;
    setup_signal_handlers();

    pthread_t th_recv, th_proc;
    pthread_create(&th_recv, NULL, receiver_thread, &ctx);
    pthread_create(&th_proc, NULL, processor_thread, &ctx);

    pthread_join(th_recv, NULL);
    pthread_join(th_proc, NULL);

    printf("Limpieza final...\n");
    freeQueue(ctx.cola);
    close(ctx.socket_cliente);
    pthread_mutex_destroy(&ctx.shutdown_mutex);
    pthread_mutex_destroy(&ctx.socket_mutex);
    printf("Consumer detenido correctamente\n");
    return 0;
}

void handle_error(const char *msg, int fatal) {
    perror(msg);
    if (fatal) exit(EXIT_FAILURE);
}

void set_nonblocking(int sockfd) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
}

void set_socket_timeout(int sockfd) {
    struct timeval tv = {.tv_sec = SOCKET_TIMEOUT_SEC, .tv_usec = 0};
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

int reconnect_to_broker(ConsumerContext *ctx, struct sockaddr_in *broker_addr) {
    time_t start = time(NULL);
    while (time(NULL) - start < RECONNECT_TIMEOUT) {
        printf("Reintentando conexion...\n");
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) { sleep(1); continue; }
        if (connect(s, (struct sockaddr*)broker_addr, sizeof(*broker_addr)) == 0) {
            pthread_mutex_lock(&ctx->socket_mutex);
            close(ctx->socket_cliente);
            ctx->socket_cliente = s;
            pthread_mutex_unlock(&ctx->socket_mutex);
            set_nonblocking(s);
            set_socket_timeout(s);
            return 1;
        }
        close(s);
        sleep(1);
    }
    // no reconecto: disparar shutdown
    pthread_mutex_lock(&ctx->shutdown_mutex);
    ctx->shutdown_flag = 1;
    pthread_mutex_unlock(&ctx->shutdown_mutex);
    pthread_mutex_lock(&ctx->cola->mutex);
    pthread_cond_broadcast(&ctx->cola->cond);
    pthread_mutex_unlock(&ctx->cola->mutex);
    return 0;
}

Queue *initQueue() {
    Queue *q = malloc(sizeof(Queue));
    q->front = q->rear = NULL;
    q->size = 0;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond, NULL);
    return q;
}

void freeQueue(Queue *q) {
    Node *cur = q->front;
    while (cur) {
        Node *nx = cur->next;
        free(cur->data);
        free(cur);
        cur = nx;
    }
    free(q);
}

void enqueue(Queue *q, void *data) {
    pthread_mutex_lock(&q->mutex);
    Node *n = malloc(sizeof(Node));
    n->data = data; n->next = NULL;
    if (!q->rear) q->front = n;
    else q->rear->next = n;
    q->rear = n;
    q->size++;
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mutex);
}

void *dequeue(Queue *q) {
    pthread_mutex_lock(&q->mutex);
    pthread_mutex_lock(&global_ctx->shutdown_mutex);
    int sd = global_ctx->shutdown_flag;
    pthread_mutex_unlock(&global_ctx->shutdown_mutex);
    while (!q->front && !sd) {
        pthread_cond_wait(&q->cond, &q->mutex);
        pthread_mutex_lock(&global_ctx->shutdown_mutex);
        sd = global_ctx->shutdown_flag;
        pthread_mutex_unlock(&global_ctx->shutdown_mutex);
    }
    if (!q->front) { pthread_mutex_unlock(&q->mutex); return NULL; }
    Node *n = q->front;
    void *d = n->data;
    q->front = n->next;
    if (!q->front) q->rear = NULL;
    free(n);
    q->size--;
    pthread_mutex_unlock(&q->mutex);
    return d;
}

void *receiver_thread(void *arg) {
    ConsumerContext *ctx = arg;
    struct pollfd fds[1];
    int timeout_ms = 1000;
    struct sockaddr_in broker_addr = { .sin_family = AF_INET, .sin_port = htons(8082), .sin_addr.s_addr = INADDR_ANY };
    Message msg;
    while (1) {
        pthread_mutex_lock(&ctx->shutdown_mutex);
        if (ctx->shutdown_flag) { pthread_mutex_unlock(&ctx->shutdown_mutex); break; }
        pthread_mutex_unlock(&ctx->shutdown_mutex);

        pthread_mutex_lock(&ctx->socket_mutex);
        fds[0].fd = ctx->socket_cliente;
        pthread_mutex_unlock(&ctx->socket_mutex);
        fds[0].events = POLLIN;

        int ret = poll(fds, 1, timeout_ms);
        if (ret > 0 && (fds[0].revents & POLLIN)) {
            ssize_t bytes = recv(fds[0].fd, &msg, sizeof(msg), 0);
            if (bytes == sizeof(msg) && msg.type == MSG_DATA) {
                Message *c = malloc(sizeof(msg));
                *c = msg;
                enqueue(ctx->cola, c);
                printf("\n[Receptor] Mensaje recibido - Offset: %u\n", msg.offset);
                Message ack = { .type=MSG_ACK, .offset=msg.offset };
                uint8_t *b = (uint8_t *)&ack;
                size_t left = sizeof(ack);
                while (left>0) { 
                    ssize_t s = send(fds[0].fd, b, left, MSG_NOSIGNAL); 
                    if (s<=0) break;
                    b+=s;
                    left-=s; 
                }
            } else if (bytes==0) {
                printf("Broker desconectado. Intentando reconexion...\n");
                if (!reconnect_to_broker(ctx,&broker_addr)) break;
            } else perror("recv");
        } else if (ret<0) {
            perror("poll");
            pthread_mutex_lock(&ctx->shutdown_mutex);
            ctx->shutdown_flag=1;
            pthread_mutex_unlock(&ctx->shutdown_mutex);
            break;
        }
    }
    printf("Hilo receptor terminado\n");
    return NULL;
}

void *processor_thread(void *arg) {
    ConsumerContext *ctx = arg;
    while (1) {
        pthread_mutex_lock(&ctx->shutdown_mutex);
        if (ctx->shutdown_flag) { pthread_mutex_unlock(&ctx->shutdown_mutex); break; }
        pthread_mutex_unlock(&ctx->shutdown_mutex);
        Message *m = dequeue(ctx->cola);
        if (!m) continue;
        printf("[Offset: %u] Origen: %s\nMessage: %s\n", 
            m->offset, m->origen, m->message);
        free(m);
    }
    printf("Hilo procesador terminado\n");
    return NULL;
}

void shutdown_handler(int sig) {
    printf("Senal %d recibida, cerrando...\n", sig);
    if (global_ctx) {
        pthread_mutex_lock(&global_ctx->shutdown_mutex);
        global_ctx->shutdown_flag = 1;
        pthread_mutex_unlock(&global_ctx->shutdown_mutex);
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
    sigaction(SIGINT,&sa,NULL);
    sigaction(SIGTERM,&sa,NULL);
}
