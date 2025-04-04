#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>

// Definición de la estructura del mensaje
typedef struct {
    int id;
    char origen[50];
    char mensaje[256];
} Mensaje;

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

    // Crear el socket TCP
    // AF_INET indica IPv4, SOCK_STREAM especifica TCP.
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        perror("Error al crear el socket");
        exit(EXIT_FAILURE);
    }

    // Configurar la estructura de la dirección del servidor
    servidor.sin_family = AF_INET;
    servidor.sin_port = htons(8080); // Puerto del servidor (broker)
    
    // Convertir la dirección IP a formato binario
    if(inet_pton(AF_INET, "127.0.0.1", &servidor.sin_addr) <= 0) {
        perror("Dirección IP no válida o no soportada");
        exit(EXIT_FAILURE);
    }

    // Conectar al servidor
    if (connect(socket_fd, (struct sockaddr *)&servidor, sizeof(servidor)) < 0) {
        perror("Error al conectar con el servidor");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }
    printf("Conexión establecida con el broker.\n");

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







// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <unistd.h>
// #include <arpa/inet.h>

// // Definición del mismo struct que usa el Producer
// typedef struct {
//     int id;
//     char origen[50];
//     char mensaje[256];
// } Mensaje;

// int main() {
//     int socket_servidor, nueva_conexion;
//     struct sockaddr_in direccion_servidor, direccion_cliente;
//     socklen_t tam_cliente = sizeof(direccion_cliente);
//     Mensaje msg;

//     // Crear el socket TCP
//     socket_servidor = socket(AF_INET, SOCK_STREAM, 0);
//     if (socket_servidor < 0) {
//         perror("Error al crear el socket del servidor");
//         exit(EXIT_FAILURE);
//     }

//     // Configurar la dirección del servidor
//     direccion_servidor.sin_family = AF_INET;
//     direccion_servidor.sin_port = htons(8080); // Puerto de escucha
//     direccion_servidor.sin_addr.s_addr = INADDR_ANY; // Aceptar conexiones en cualquier IP local

//     // Enlazar el socket con la dirección y puerto
//     if (bind(socket_servidor, (struct sockaddr *)&direccion_servidor, sizeof(direccion_servidor)) < 0) {
//         perror("Error al hacer bind");
//         close(socket_servidor);
//         exit(EXIT_FAILURE);
//     }

//     // Escuchar conexiones entrantes
//     if (listen(socket_servidor, 5) < 0) {
//         perror("Error al escuchar");
//         close(socket_servidor);
//         exit(EXIT_FAILURE);
//     }

//     printf("Broker escuchando en el puerto 8080...\n");

//     // Ciclo infinito para aceptar y procesar mensajes
//     while (1) {
//         nueva_conexion = accept(socket_servidor, (struct sockaddr *)&direccion_cliente, &tam_cliente);
//         if (nueva_conexion < 0) {
//             perror("Error al aceptar conexión");
//             continue;
//         }

//         // Recibir el mensaje
//         ssize_t bytes_recibidos = recv(nueva_conexion, &msg, sizeof(Mensaje), 0);
//         if (bytes_recibidos <= 0) {
//             perror("Error al recibir mensaje o conexión cerrada");
//         } else {
//             printf("Mensaje recibido:\n");
//             printf("  ID: %d\n", msg.id);
//             printf("  Origen: %s\n", msg.origen);
//             printf("  Contenido: %s\n\n", msg.mensaje);
//         }

//         close(nueva_conexion);
//     }

//     // Nunca se llega aquí en este ejemplo, pero sería buena práctica
//     close(socket_servidor);
//     return 0;
// }
