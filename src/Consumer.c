#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

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
} Queue;*/

extern Queue mensajes; // Cola de mensajes

extern Queue *initQueue(); // Inicializa la cola
extern void enqueue(Queue *queue, void *data); // Agrega un elemento a la cola
extern void *dequeue(Queue *queue); // Elimina y devuelve el primer elemento de la cola
extern int isEmpty(Queue *queue); // Verifica si la cola está vacía
extern int getSize(Queue *queue); // Devuelve el tamaño de la cola
extern void freeQueue(Queue *queue); // Libera la memoria de la cola

typedef struct {
    int id;
    char origen[50];
    char mensaje[256];
} Mensaje;

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
    socket_servidor = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_servidor < 0) {
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
    direccion_servidor.sin_addr.s_addr = INADDR_ANY; // Dirección del servidor (localhost)

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
    fd_set read_fds;
    struct timeval timeout;

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(socket_cliente, &read_fds);

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