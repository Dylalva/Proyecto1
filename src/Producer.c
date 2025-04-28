#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>

// --------------------
// Declaraciones de estructuras y funciones
// --------------------

// Definición de la estructura del mensaje
typedef struct {
    int id;
    char origen[50];
    char mensaje[256];
} Mensaje;

// Declaración de funciones
void obtener_identificador(char *identificador, size_t size);

int main();

// --------------------
// Función principal
// --------------------

int main() {
    int socket_fd;
    struct sockaddr_in servidor;
    Mensaje msg;
    char identificador[64];
    obtener_identificador(identificador, sizeof(identificador));

    // Inicializar el mensaje a enviar
    msg.id = 1;
    strcpy(msg.origen, identificador);
    strcpy(msg.mensaje, "Hola, este es un mensaje de prueba!");

    // Configurar la estructura de la dirección del servidor
    servidor.sin_family = AF_INET;
    servidor.sin_port = htons(8081); // Puerto del servidor (broker)
    if (inet_pton(AF_INET, "127.0.0.1", &servidor.sin_addr) <= 0) {
        perror("Dirección IP no válida o no soportada");
        exit(EXIT_FAILURE);
    }

    // Intentar conectarse al broker con un mecanismo de reconexión
    time_t start_time = time(NULL); // Tiempo inicial
    while (1) {
        // Crear el socket TCP
        socket_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_fd < 0) {
            perror("Error al crear el socket");
            exit(EXIT_FAILURE);
        }

        // Intentar conectarse al broker
        if (connect(socket_fd, (struct sockaddr *)&servidor, sizeof(servidor)) == 0) {
            printf("Conexión establecida con el broker.\n");
            break; // Salir del bucle si la conexión es exitosa
        }

        // Si no se pudo conectar, cerrar el socket y verificar el tiempo transcurrido
        perror("Error al conectar con el servidor. Reintentando...");
        close(socket_fd);

        if (time(NULL) - start_time >= 30) { // Verificar si han pasado 30 segundos
            fprintf(stderr, "No se pudo conectar con el broker después de 30 segundos. Terminando el proceso.\n");
            exit(EXIT_FAILURE);
        }

        sleep(1); // Esperar 1 segundo antes de reintentar
    }

    // Enviar el mensaje
    ssize_t bytes_enviados = send(socket_fd, &msg, sizeof(Mensaje), 0);
    if (bytes_enviados < 0) {
        perror("Error al enviar el mensaje");
    } else {
        printf("Mensaje enviado: %s\n", msg.mensaje);
    }

    // Cerrar el socket
    close(socket_fd);
    printf("Conexión cerrada.\n");
    return 0;
}

// --------------------
// Definiciones de funciones
// --------------------

void obtener_identificador(char *identificador, size_t size) {
    char hostname[256];
    struct hostent *host_entry;
    char *ip;
    pid_t pid = getpid();

    // Obtener nombre del host
    if (gethostname(hostname, sizeof(hostname)) == -1) {
        perror("gethostname");
        exit(EXIT_FAILURE);
    }

    // Obtener IP asociada al nombre del host
    host_entry = gethostbyname(hostname);
    if (host_entry == NULL) {
        perror("gethostbyname");
        exit(EXIT_FAILURE);
    }

    ip = inet_ntoa(*((struct in_addr*)host_entry->h_addr_list[0]));

    // Combinar IP y PID como identificador
    snprintf(identificador, size, "%s-%d", ip, pid);
}
