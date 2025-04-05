#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <sys/select.h>
#include <pthread.h>

/*// Nodo de la cola
typedef struct Node {    
    void *data;             // Puntero genérico para almacenar cualquier tipo de dato    
    struct Node *next;      // Puntero al siguiente nodo
} Node;

// Estructura de la cola
typedef struct Queue {    
    Node *front;            // Puntero al frente de la cola    
    Node *rear;             // Puntero al final de la cola    
    int size;               // Tamaño actual de la cola
} Queue;

Queue mensajes; // Cola de mensajes

extern Queue *initQueue(); // Inicializa la cola
extern void enqueue(Queue *queue, void *data); // Agrega un elemento a la cola
extern void *dequeue(Queue *queue); // Elimina y devuelve el primer elemento de la cola
extern int isEmpty(Queue *queue); // Verifica si la cola está vacía
extern int getSize(Queue *queue); // Devuelve el tamaño de la cola
extern void freeQueue(Queue *queue); // Libera la memoria de la cola

            Esta parte es con el extern pero no esta funcionando porque colisionan los main */ 

// Nodo de la cola
typedef struct Node {    
    void *data;             // Puntero genérico para almacenar cualquier tipo de dato    
    struct Node *next;      // Puntero al siguiente nodo
} Node;

// Estructura de la cola
typedef struct Queue {    
    Node *front;            // Puntero al frente de la cola    
    Node *rear;             // Puntero al final de la cola    
    int size;               // Tamaño actual de la cola
    pthread_mutex_t mutex;  // Mutex para sincronizar el acceso a la cola
} Queue;

// Función para inicializar la cola
Queue *initQueue() {    
    Queue *queue = (Queue *)malloc(sizeof(Queue));    
    if (!queue) {        
        perror("Error al asignar memoria para la cola");        
        exit(EXIT_FAILURE);    
    }    queue->front = NULL;    
    queue->rear = NULL;    
    queue->size = 0;    
    pthread_mutex_init(&queue->mutex, NULL); // Inicializar el mutex
    return queue;
}

// Función para agregar un elemento a la cola
void enqueue(Queue *queue, void *data) {    
    Node *newNode = (Node *)malloc(sizeof(Node));    
    if (!newNode) {        
        perror("Error al asignar memoria para el nodo");        
        exit(EXIT_FAILURE);    
    }    
    newNode->data = data;    
    newNode->next = NULL;

    pthread_mutex_lock(&queue->mutex); // Bloquear el mutex
    if (queue->rear == NULL) {        
        queue->front = newNode;       
        queue->rear = newNode;   
    } else {        
        queue->rear->next = newNode;        
        queue->rear = newNode;    
    }    
    queue->size++;
    pthread_mutex_unlock(&queue->mutex); // Desbloquear el mutex
}

// Función para eliminar un elemento de la cola
void *dequeue(Queue *queue) {    
    pthread_mutex_lock(&queue->mutex); // Bloquear el mutex
    if (queue->front == NULL) {        
        printf("La cola está vacía\n");      
        pthread_mutex_unlock(&queue->mutex); // Desbloquear el mutex    
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
    pthread_mutex_unlock(&queue->mutex); // Desbloquear el mutex  
    return data;
}

int isEmpty(Queue *queue) {    
    return queue->size == 0;
}

// Función para liberar la memoria de la cola
void freeQueue(Queue *queue) {  
    pthread_mutex_lock(&queue->mutex); // Bloquear el mutex  
    while (!isEmpty(queue)) {        
        dequeue(queue);    
    }    
    pthread_mutex_unlock(&queue->mutex); // Desbloquear el mutex
    pthread_mutex_destroy(&queue->mutex); // Destruir el mutex
    free(queue);
}

typedef struct {
    int id;
    char origen[50];
    char mensaje[256];
} Mensaje;

// Función para configurar un socket como no bloqueante, es decir siempre se mantiene corriendo
void setNonBlocking(int socket) {
    int flags = fcntl(socket, F_GETFL, 0);
    if (flags == -1) {
        perror("Error al obtener flags del socket");
        exit(EXIT_FAILURE);
    }
    if (fcntl(socket, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("Error al configurar el socket como no bloqueante");
        exit(EXIT_FAILURE);
    }
}

typedef struct{
    int socket_cliente; // Socket del cliente
    Queue *cola;        // Cola para almacenar los mensajes
} ConsumerArgs;

// Función para recibir mensajes del servidor y almacenarlos en la cola
void *receiveMessages(void *arg) {
    ConsumerArgs *args = (ConsumerArgs *)arg; // Convertir el argumento al tipo correcto
    int socket_cliente = args->socket_cliente; // Obtener el socket del cliente
    Queue *mensajes = args->cola; // Obtener la cola de mensajes
    Mensaje msg;

    while (1) {
        ssize_t bytes_recibidos = recv(socket_cliente, &msg, sizeof(Mensaje), 0);
        if (bytes_recibidos > 0) {
            // Almacenar el mensaje en la cola
            Mensaje *msg_ptr = malloc(sizeof(Mensaje));
            if (!msg_ptr) {
                perror("Error al asignar memoria para el mensaje");
            } else {
                memcpy(msg_ptr, &msg, sizeof(Mensaje));
                enqueue(mensajes, msg_ptr);
                printf("Mensaje agregado a la cola. Tamaño actual de la cola: %d\n", mensajes->size);
            }
        } else if (bytes_recibidos == 0) {
            printf("El servidor cerró la conexión.\n");
            break;
        }  else {
            // Error al recibir el mensaje
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No hay datos disponibles temporalmente, continuar
                usleep(100000); // Esperar 100 ms antes de intentar nuevamente
                continue;
            } else {
                // Otro error ocurrió
                perror("Error al recibir el mensaje");
                break;
            }
        }
    }

    free(args); // Liberar la memoria de los argumentos
    return NULL;
}

// Función para mostrar los mensajes en la cola
void *showMessages(void *arg) {
    Queue *cola = (Queue *)arg; // Obtener la cola de mensajes

    while (1) {
        if (!isEmpty(cola)) {
            Mensaje *msg = (Mensaje *)dequeue(cola);
            if (msg) {
                printf("Procesando mensaje de la cola:\n");
                printf("  ID: %d\n", msg->id);
                printf("  Origen: %s\n", msg->origen);
                printf("  Contenido: %s\n", msg->mensaje);
                free(msg); // Liberar la memoria del mensaje procesado
            }
        } else {
            // Si la cola está vacía, esperar un momento antes de volver a verificar
            usleep(500000); // 500 ms
        }
    }

    return NULL;
}


// -------------------------
// Implementación del Consumer con Cola
// -------------------------

int main(){
    int socket_cliente;
    struct sockaddr_in direccion_servidor;

    // Inicializar la cola
    Queue *mensajes = initQueue();

    // Crear el socket TCP
    socket_cliente = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_cliente < 0) {
        perror("Error al crear el socket del cliente");
        exit(EXIT_FAILURE);
    }

    // Configurar la dirección del servidor
    direccion_servidor.sin_family = AF_INET;
    direccion_servidor.sin_port = htons(8080); // Puerto del servidor
    direccion_servidor.sin_addr.s_addr = INADDR_ANY; // Dirección del servidor

    // Configurar el socket como no bloqueante
    setNonBlocking(socket_cliente);

    // Intentar conectarse al servidor
    if (connect(socket_cliente, (struct sockaddr *)&direccion_servidor, sizeof(direccion_servidor)) < 0) {
        if (errno != EINPROGRESS) {
            perror("Error al intentar conectarse al servidor");
            close(socket_cliente);
            exit(EXIT_FAILURE);
        }
    }

    printf("Conectado al servidor...\n");

    // Crear los hilos
    pthread_t hilo_receptor, hilo_mostrador;

    // Crear los argumentos para el hilo receptor
    ConsumerArgs *args = malloc(sizeof(ConsumerArgs));
    if (!args) {
        perror("Error al asignar memoria para los argumentos del hilo receptor");
        exit(EXIT_FAILURE);
    }
    args->socket_cliente = socket_cliente;
    args->cola = mensajes;

    // Crear el hilo para recibir mensajes
    if (pthread_create(&hilo_receptor, NULL, receiveMessages, args) != 0) {
        perror("Error al crear el hilo receptor");
        free(args); // Liberar memoria en caso de error
        freeQueue(mensajes);
        close(socket_cliente);
        exit(EXIT_FAILURE);
    }

    // Crear el hilo para mostrar mensajes
    if (pthread_create(&hilo_mostrador, NULL, showMessages, mensajes) != 0) {
        perror("Error al crear el hilo mostrador");
        pthread_cancel(hilo_receptor); // Cancelar el hilo receptor si ya fue creado
        pthread_join(hilo_receptor, NULL); // Esperar a que termine el hilo receptor
        freeQueue(mensajes);
        close(socket_cliente);
        exit(EXIT_FAILURE);
    }

    // Esperar a que los hilos terminen
    pthread_join(hilo_receptor, NULL);
    pthread_join(hilo_mostrador, NULL);

    /*
    En caso de error
        if (pthread_join(hilo_receptor, NULL) != 0) {
            perror("Error al esperar el hilo receptor");
        }

        if (pthread_join(hilo_mostrador, NULL) != 0) {
            perror("Error al esperar el hilo mostrador");
        }
    */

    // Liberar la memoria de la cola y cerrar el socket
    freeQueue(mensajes);
    close(socket_cliente);

    return 0;
}