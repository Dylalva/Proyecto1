#include <stdio.h>
#include <stdlib.h>
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