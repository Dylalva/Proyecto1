#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
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


// Función para imprimir y enviar mensaje al socket del consumer
/*
// Función para enviar un mensaje al cliente
void enviarMensaje(int socket_cliente, Mensaje *msg) {
    if (send(socket_cliente, msg, sizeof(Mensaje), 0) < 0) {
        perror("Error al enviar el mensaje");
    } else {
        printf("Mensaje enviado al cliente:\n");
        printf("  ID: %d\n", msg->id);
        printf("  Origen: %s\n", msg->origen);
        printf("  Contenido: %s\n", msg->mensaje);
    }
}
*/

// -------------------------
// Implementación del Servidor con Cola
// -------------------------

int main() {
    int socket_servidor, nueva_conexion;
    struct sockaddr_in direccion_servidor, direccion_cliente;
    socklen_t tam_cliente = sizeof(direccion_cliente);
    Mensaje msg;
    int id = 0;

    // Inicializar la cola
    Queue *cola = initQueue();

    // Crear el socket TCP
    socket_servidor = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_servidor < 0) {
        perror("Error al crear el socket del servidor");
        exit(EXIT_FAILURE);
    }

    // Configurar la dirección del servidor
    direccion_servidor.sin_family = AF_INET;
    direccion_servidor.sin_port = htons(8080); // Puerto de escucha
    direccion_servidor.sin_addr.s_addr = INADDR_ANY; // Aceptar conexiones en cualquier IP local

    // Enlazar el socket con la dirección y puerto
    if (bind(socket_servidor, (struct sockaddr *)&direccion_servidor, sizeof(direccion_servidor)) < 0) {
        perror("Error al hacer bind");
        close(socket_servidor);
        exit(EXIT_FAILURE);
    }

    // Escuchar conexiones entrantes
    if (listen(socket_servidor, 5) < 0) {
        perror("Error al escuchar");
        close(socket_servidor);
        exit(EXIT_FAILURE);
    }

    printf("Broker escuchando en el puerto 8080...\n");

    // Ciclo infinito para aceptar y procesar mensajes
    while (1) {
        nueva_conexion = accept(socket_servidor, (struct sockaddr *)&direccion_cliente, &tam_cliente);
        if (nueva_conexion < 0) {
            perror("Error al aceptar conexión");
            continue;
        }

        // Recibir el mensaje
        size_t bytes_recibidos = recv(nueva_conexion, &msg, sizeof(Mensaje), 0);
        msg.id = id++;
        if (bytes_recibidos <= 0) {
            perror("Error al recibir mensaje o conexión cerrada");
        } else {
            printf("\nMensaje recibido:\n");
            printf("  ID: %d\n", msg.id);
            printf("  Origen: %s\n", msg.origen);
            printf("  Contenido: %s\n", msg.mensaje);

            // Almacenar el mensaje en la cola (se reserva memoria para almacenar el mensaje recibido)
            Mensaje *msg_ptr = malloc(sizeof(Mensaje));
            if (!msg_ptr) {
                perror("Error al asignar memoria para el mensaje");
                close(nueva_conexion);
                continue;
            }
            memcpy(msg_ptr, &msg, sizeof(Mensaje));
            enqueue(cola, msg_ptr);
        }

        // Enviar un mensaje de prueba al consumer (descomentar para probar)
        /*
        msg.id = 1; // ID del mensaje
        strcpy(msg.origen, "Servidor"); // Origen del mensaje
        strcpy(msg.mensaje, "Hola desde el servidor"); // Contenido del mensaje
        enviarMensaje(nueva_conexion, &msg);
        */

        close(nueva_conexion);

        // Imprimir el contenido actual de la cola para la prueba
        printf("Contenido actual de la cola:\n");
        Node *actual = cola->front;
        while (actual != NULL) {
            Mensaje *m = (Mensaje *)actual->data;
            printf("  ID: %d, Origen: %s, Contenido: %s\n", m->id, m->origen, m->mensaje);
            actual = actual->next;
        }
        printf("\n");
        
    }

    // Liberar la memoria de la cola (aunque en este ejemplo el ciclo es infinito)
    freeQueue(cola);
    close(socket_servidor);
    return 0;
}