#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/file.h>
#include <errno.h>

// -------------------------
// Configuraciones
// -------------------------
#define MAX_FILENAME_LEN   256
#define MAX_TMPFILE_LEN    (MAX_FILENAME_LEN + 4)  // + .tmp
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
} Queue;

typedef struct {
    int socket_cliente;
    Queue *cola;
    long current_offset;
    pthread_mutex_t offset_mutex;
} ConsumerContext;

// -------------------------
// Funciones de la Cola
// -------------------------
Queue *initQueue() {    
    Queue *queue = malloc(sizeof(Queue));    
    queue->front = NULL;    
    queue->rear = NULL;    
    queue->size = 0;    
    pthread_mutex_init(&queue->mutex, NULL);    
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
    pthread_mutex_unlock(&queue->mutex);
}

void *dequeue(Queue *queue) {    
    pthread_mutex_lock(&queue->mutex);    
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
    if (!file) {
        if (errno != ENOENT) perror("Error abriendo archivo de offset");
        return 0;
    }
    
    // Bloqueo compartido para lectura
    if (flock(fileno(file), LOCK_SH) == -1) {
        perror("Error al bloquear archivo");
        fclose(file);
        return 0;
    }
    
    long offset = 0;
    if (fscanf(file, "%ld", &offset) != 1) {
        fprintf(stderr, "Error leyendo offset\n");
    }
    
    flock(fileno(file), LOCK_UN);
    fclose(file);
    return offset;
}

void guardar_offset(long offset) {
    const char* filename = "offset.log";
    const char* tmpfile = "offset.tmp";

    // Crear archivo temporal
    FILE* file = fopen(tmpfile, "w");
    if (!file) {
        perror("Error creando archivo temporal");
        return;
    }

    // Escribir el offset
    if (fprintf(file, "%ld", offset) < 0) {
        perror("Error escribiendo offset");
        fclose(file);
        remove(tmpfile);
        return;
    }

    // Forzar escritura a disco
    if (fflush(file) != 0) {
        perror("Error sincronizando datos");
        fclose(file);
        remove(tmpfile);
        return;
    }

    // Cerrar archivo
    if (fclose(file) != 0) {
        perror("Error cerrando archivo temporal");
        remove(tmpfile);
        return;
    }

    // Renombrar atómicamente
    if (rename(tmpfile, filename) != 0) {
        perror("Error renombrando archivo");
        remove(tmpfile);
        return;
    }

    // Sincronizar directorio
    int dirfd = open(".", O_RDONLY);
    fsync(dirfd);
    close(dirfd);
}

// -------------------------
// Hilos de Trabajo
// -------------------------
void *receiver_thread(void *arg) {
    ConsumerContext *ctx = (ConsumerContext *)arg;
    Message msg;
    
    while (1) {
        ssize_t bytes = recv(ctx->socket_cliente, &msg, sizeof(Message), 0);
        
        if (bytes > 0) {
            Message *msg_copy = malloc(sizeof(Message));
            memcpy(msg_copy, &msg, sizeof(Message));
            enqueue(ctx->cola, msg_copy);
            printf("[Receptor] Mensaje recibido - Offset: %ld\n", msg.offset);
        } 
        else if (bytes == 0) {
            printf("Conexión cerrada por el broker\n");
            break;
        } 
        else {
            perror("Error en recepción");
            break;
        }
    }
    
    close(ctx->socket_cliente);
    return NULL;
}

void *processor_thread(void *arg) {
    ConsumerContext *ctx = (ConsumerContext *)arg;
    
    while (1) {
        Message *msg = dequeue(ctx->cola);
        if (msg) {
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
        usleep(100000); // Reducir consumo de CPU
    }
    return NULL;
}

// -------------------------
// Configuración Principal
// -------------------------
int main(int argc, char *argv[]) {

    ConsumerContext ctx;
    
    // Cargar offset desde archivo, si no existe, se inicializa en 0
    ctx.current_offset = cargar_offset();

    ctx.cola = initQueue();
    pthread_mutex_init(&ctx.offset_mutex, NULL);

    // Configurar socket
    ctx.socket_cliente = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in broker_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(8080),
        .sin_addr.s_addr = inet_addr("127.0.0.1")
    };
    
    // Conexión
    if (connect(ctx.socket_cliente, (struct sockaddr*)&broker_addr, sizeof(broker_addr)) < 0) {
        perror("Error de conexión");
        exit(EXIT_FAILURE);
    }
    
    // Registro inicial
    send(ctx.socket_cliente, "REGISTER", 8, 0);

    // Hilos
    pthread_t t_receiver, t_processor;
    pthread_create(&t_receiver, NULL, receiver_thread, &ctx);
    pthread_create(&t_processor, NULL, processor_thread, &ctx);

    pthread_join(t_receiver, NULL);
    pthread_join(t_processor, NULL);

    // Limpieza
    pthread_mutex_destroy(&ctx.offset_mutex);
    free(ctx.cola->front);
    free(ctx.cola);
    close(ctx.socket_cliente);

    return 0;
}