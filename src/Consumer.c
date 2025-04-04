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
    if (queue->rear == NULL) {        
        queue->front = newNode;       
        queue->rear = newNode;   
    } else {        
        queue->rear->next = newNode;        
        queue->rear = newNode;    
    }    
    queue->size++;
}

// Función para eliminar un elemento de la cola
void *dequeue(Queue *queue) {    
    if (queue->front == NULL) {        
        printf("La cola está vacía\n");        
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

// Función para liberar la memoria de la cola
void freeQueue(Queue *queue) {    
    while (!isEmpty(queue)) {        
        dequeue(queue);    
    }    
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

// -------------------------
// Implementación del Consumer con Cola
// -------------------------

int main(){
    int socket_cliente;
    struct sockaddr_in direccion_servidor;
    Mensaje msg;

    // Inicializar la cola
    Queue* mensajes = initQueue();

    // Crear el socket TCP
    socket_cliente = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_cliente < 0) {
        perror("Error al crear el socket del servidor");
        exit(EXIT_FAILURE);
    }


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

    printf("Conectando al servidor...\n");

    // Usar select() para manejar la recepción de datos
    fd_set read_fds;        // Conjunto de descriptores de lectura 
    struct timeval timeout; // Tiempo de espera para select()

    while (1) {
        FD_ZERO(&read_fds);                 // Limpiar el conjunto de descriptores
        FD_SET(socket_cliente, &read_fds);  // Agregar el socket del cliente al conjunto

        timeout.tv_sec = 5; // Tiempo de espera de 5 segundos
        timeout.tv_usec = 0;

        int result = select(socket_cliente + 1, &read_fds, NULL, NULL, &timeout);
        if (result > 0 && FD_ISSET(socket_cliente, &read_fds)) {
            // Recibir el mensaje del servidor
            ssize_t bytes_recibidos = recv(socket_cliente, &msg, sizeof(Mensaje), 0);
            if (bytes_recibidos > 0) {
                printf("Mensaje recibido del servidor:\n");
                printf("  ID: %d\n", msg.id);
                printf("  Origen: %s\n", msg.origen);
                printf("  Contenido: %s\n", msg.mensaje);

                // Almacenar el mensaje en la cola
                Mensaje* msg_ptr = malloc(sizeof(Mensaje));
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
            } else {
                perror("Error al recibir el mensaje");
            }
        } else if (result == 0) {
            printf("Esperando mensajes del servidor...\n");
        } else {
            perror("Error en select()");
            break;
        }
    }

    // Liberar la memoria de la cola y cerrar el socket
    freeQueue(mensajes);
    close(socket_cliente);

    return 0;
}