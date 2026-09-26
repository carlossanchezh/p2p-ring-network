// FUNCIONALIDAD DE LA PARTE CLIENTE
#include <sys/mman.h>
#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <sys/stat.h>

#include "ring.h"
#include "common.h"

//Variables globales pra que se recuerde la configuración del nodo
char shared_dir[1024]; //Directorio para guardar los ficheros descargados
unsigned int my_ip;  // IP propia del nodo
unsigned short my_port; // Puerto propio del nodo
unsigned int succ_ip;  //IP del sucesor
unsigned short succ_port;  //Puerto del sucesor
static int srv_socket; // Socket para escuchar al servidor
static int initialized = 0; //Flag para saber si esta inicializado

static int is_initialized(void);
static int initialize(void);

// inicia el nodo añadiéndolo a la red P2P si ya está creada;
// los puertos e IPs deben estar en formato de red;
// debe devolver en el último parámetro el puerto reservado en formato red;
// retorna 0 si OK y -1 si error
int ring_init(const char *shrd_dir, unsigned int local_ip, unsigned int remote_ip, unsigned short remote_port, unsigned short *alloc_port) {
    if (initialize()) return -1; // ya está inicializada

    //Guardar el directorio compartido
    strncpy(shared_dir, shrd_dir, sizeof(shared_dir)-1);
    shared_dir[sizeof(shared_dir)-1] = '\0';

    //Guardar IP propia
    my_ip = local_ip;

    //Crear socket servidor
    srv_socket = create_socket_srv(&my_port);
    if (srv_socket < 0) return -1;

    //Devolver el puerto asignado
    if (alloc_port) *alloc_port = my_port;

    //Lanzar thread servidor
    if (create_thread(server_thread, (void*)(long)srv_socket) != 0) {
        close(srv_socket);
        return -1;
    }

    //Inicializar sucesor (si no hay nodo remoto soy mi propio sucesor)
    if (remote_ip == 0) {
        
        succ_ip = my_ip;
        succ_port = my_port;

    } else {
        
        // Conexion con el nodo remoto
        int sock = create_socket_cln(remote_ip,remote_port); //se crea el socket y se conecta con el nodo remoto
        if(sock<0) return -1;

        // COdigo 'A' definido apra la operacion de Añadir un nuevo Nodo
        // Enviar el codigo A 
        char op = 'A';
        struct iovec iov[2];
        iov[0].iov_base = &op; //enviar codigo de operacion a traves de writev
        iov[0].iov_len  = 1;
        iov[1].iov_base = &my_port; //enviar puerto a traves de writev
        iov[1].iov_len  = sizeof(my_port);
        if(writev(sock, iov, 2) != 1+sizeof(my_port)) { //se envia el codigo de operacion A a traves del socket
            close(sock);
            return -1;
        }

        //Recibir Ip y puerto del nodo sucesor 
        unsigned int new_succ_ip;
        unsigned short new_succ_port;

        if (recv(sock, &new_succ_ip, sizeof(new_succ_ip), MSG_WAITALL) != sizeof(new_succ_ip)) { //recibir ip 
        close(sock);
        return -1;
        }

        if (recv(sock, &new_succ_port, sizeof(new_succ_port), MSG_WAITALL) != sizeof(new_succ_port)) { //recibir port
        close(sock);
        return -1;
        }

        close(sock);

        //Guardar el nuevo sucesor 
        succ_ip = new_succ_ip;
        succ_port = new_succ_port;

    }

    initialized = 1; // Flag de inicializacion activo

    return 0;
}
// función local que devuelve la IP y el puerto del nodo;
// retorna 0 si OK y -1 si error
int ring_self(unsigned int *ip, unsigned short *port) {
    if (!is_initialized()) return -1; // no está inicializada

    if (ip) *ip = my_ip; //Guardar IP del nodo
    if (port) *port = my_port; //Guardar puerto del nodo

    return 0;
}
// devuelve el PID del nodo remoto especificado o -1 si error
int ring_remote_pid(unsigned int remote_ip, unsigned short remote_port) {
    if (!is_initialized()) return -1; // no está inicializada

    int sock = create_socket_cln(remote_ip, remote_port); // se crea el socket y se conecta al nodo remoto
    if (sock < 0) return -1;

    char op = 'P';
    if (send(sock, &op, 1, 0) != 1) { //se envia el codigo de operacion P al servidor a traves del socket
        close(sock);
        return -1;
    }

    int pid_net;
    if (recv(sock, &pid_net, sizeof(pid_net), MSG_WAITALL) != sizeof(pid_net)) { //se recibe el PID en foramto de red del servidor a traves del socket
        close(sock);
        return -1;
    }

    close(sock); //se cierra el socket de comunicacion con el servidor 

    return ntohl(pid_net); //se convierte el PID de formato red a formato local para devolverlo
}
// función local que devuelve la IP y el puerto del nodo sucesor;
// retorna 0 si OK y -1 si error
int ring_successor(unsigned int *ip, unsigned short *port) {
    if (!is_initialized()) return -1; // no está inicializada

    if(ip) *ip = succ_ip; //guarda la ip del sucesor
    if(port) *port = succ_port; //guarda el port del sucesor

    return 0;
}
// devuelve la IP y el puerto del nodo sucesor del especificado;
// retorna 0 si OK y -1 si error
int ring_remote_successor(unsigned int remote_ip, unsigned short remote_port, unsigned int *suc_ip, unsigned short *suc_port) {
    if (!is_initialized()) return -1; // no está inicializada
    
    int sock = create_socket_cln(remote_ip, remote_port); //se crea socket y se conecta al nodo remoto
    if (sock < 0) return -1;

    //Enviar codigo 'S' para pregunatr por el sucesor
    char op = 'S';
    if (send(sock, &op, 1, 0) != 1) {
        close(sock);
        return -1;
    }

    //Recibir respuesta del servidor
    unsigned int ip;
    unsigned short port;

    if (recv(sock, &ip, sizeof(ip), MSG_WAITALL) != sizeof(ip)) {
        close(sock);
        return -1;
    }
    if (recv(sock, &port, sizeof(port), MSG_WAITALL) != sizeof(port)) {
        close(sock);
        return -1;
    }

    close(sock);//Cerrar el socket

    //Devolver los valors de ip y puerto
    if (suc_ip) *suc_ip = ip;
    if (suc_port) *suc_port = port;

    return 0;
}
// devuelve la IP y el puerto del nodo sucesor del sucesor del especificado;
// retorna 0 si OK y -1 si error
int ring_remote_successor_successor(unsigned int remote_ip, unsigned short remote_port, unsigned int *suc_suc_ip, unsigned short *suc_suc_port) {
    if (!is_initialized()) return -1; // no está inicializada
    
    int sock = create_socket_cln(remote_ip, remote_port); //crear socket y conectar al nodo remoto
    if (sock < 0) return -1;

    //Enviar codigo 'U' para pregunatr por el sucesor del sucesor
    char op = 'U';
    if (send(sock, &op, 1, 0) != 1) {
        close(sock);
        return -1;
    }

    //Recibir ip y puerto de respuesta 
    unsigned int ip;
    unsigned short port;

    if (recv(sock, &ip, sizeof(ip), MSG_WAITALL) != sizeof(ip)) {
        close(sock);
        return -1;
    }
    if (recv(sock, &port, sizeof(port), MSG_WAITALL) != sizeof(port)) {
        close(sock);
        return -1;
    }

    //comprobar si se encontro el succesor del sucesor si no se encontro se recibe ip y port 0
    if (ip == 0 && port == 0) {
        return -1;  //no encontro nodo
    }

    close(sock);//cerrar el socket

    // Devolver valores de ip y puerto
    if (suc_suc_ip) *suc_suc_ip = ip;
    if (suc_suc_port) *suc_suc_port = port;
    
    return 0;

}
// descarga el fichero del nodo especificado;
// retorna el tamaño del fichero si OK y -1 en caso de error
int ring_download(unsigned int remote_ip, unsigned short remote_port, const char *filename) {
    if (!is_initialized()) return -1; // no está inicializada
    
    //Crear socket y conectar con el nodo remoto
    int sock = create_socket_cln(remote_ip, remote_port);
    if (sock < 0) return -1;

    //Enviar el codigo de operacion D para descargar el fihero, longitud y nombre del fichero
    char op = 'D';
    int name_len = strlen(filename);
    int name_len_net = htonl(name_len);

    struct iovec iov[3];
    iov[0].iov_base = &op; //operacion
    iov[0].iov_len  = 1;
    iov[1].iov_base = &name_len_net;//longitud
    iov[1].iov_len = sizeof(name_len_net);
    iov[2].iov_base = (void*)filename; //nombre 
    iov[2].iov_len = name_len; 
    
    
    if (writev(sock, iov, 3) != 1+ sizeof(name_len_net) + name_len) {
        close(sock);
        return -1;
    }

    //Recibir tamaño del fichero
    int size_net;
    if (recv(sock, &size_net, sizeof(size_net), MSG_WAITALL) != sizeof(size_net)) {
        close(sock);
        return -1;
    }

    int file_size = ntohl(size_net);
    
    if (file_size == -1) { //Si el tamaño es -1 error
        close(sock);
        return -1;
    }

    //Contruir ruta del fichero
    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%s/%s", shared_dir, filename);

    //Crear fichero
    int fd = open(filepath, O_CREAT | O_TRUNC | O_RDWR, 0666);
    if (fd < 0) {
        close(sock);
        return -1;
    }

    //Tamño de fichero ajustado al tamaño recibido
    if (ftruncate(fd, file_size) < 0) {
        close(fd);
        close(sock);
        return -1;
    }

    //mmap para fichero en memoria
    char *mapped = mmap(NULL, file_size, PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        close(fd);
        close(sock);
        return -1;
    }

    //Recibir datos en la region del mmap
    ssize_t received = 0;
    while (received < file_size) {
        ssize_t n = recv(sock, mapped + received, file_size - received, 0);
        if (n <= 0) {
            munmap(mapped, file_size);
            close(fd);
            close(sock);
            return -1;
        }
        received += n;
    }

    //Limpiar memoria
    munmap(mapped, file_size);
    close(fd);
    close(sock);
    
    return file_size;
}

// busca el fichero en el anillo dando un número máximo de saltos y devolviendo
// la IP y el puerto del nodo que lo contiene;
// retorna 0 si OK y -1 si error
int ring_lookup(const char *filename, int hops, unsigned int *ip, unsigned short *port) {
    if (!is_initialized()) return -1; // no está inicializada
    
    //Crear socket y conectar con el propio nodo (el fichero podria tenerlo el mismo)
    int sock = create_socket_cln(my_ip, my_port);
    if (sock < 0) return -1;

    //Enviar el codigo de operacion L, hops, longitud y nombre
    char op = 'L';
    int hops_net = htonl(hops); //formato red
    int name_len = strlen(filename);
    int name_len_net = htonl(name_len); //formato red

    struct iovec iov[4];
    iov[0].iov_base = &op; //operacion
    iov[0].iov_len  = 1;
    iov[1].iov_base = &hops_net; //hops
    iov[1].iov_len = sizeof(hops_net);
    iov[2].iov_base = &name_len_net; //longitud
    iov[2].iov_len = sizeof(name_len_net);
    iov[3].iov_base = (void*)filename; //nombre
    iov[3].iov_len = name_len;

    if (writev(sock, iov, 4) != 1 + sizeof(hops_net) + sizeof(name_len_net) + name_len) {
        close(sock);
        return -1;
    }

    //Recibir Ip y puerto del nodo que tiene el fichero buscado
    unsigned int res_ip;
    unsigned short res_port;

    if (recv(sock, &res_ip, sizeof(res_ip), MSG_WAITALL) != sizeof(res_ip)) {
        close(sock);
        return -1;
    }

    if (recv(sock, &res_port, sizeof(res_port), MSG_WAITALL) != sizeof(res_port)) {
        close(sock);
        return -1;
    }

    close(sock); //cerrar socket ya no se va a usar 

    // Si la respuesta es 0, el fichero no se encontró
    if (res_ip == 0 && res_port == 0) {
        return -1;
    }

    // Devolver valores de ip y puerto si no son null
    if (ip) *ip = res_ip;
    if (port) *port = res_port;

    return 0;

}
// busca y descarga el fichero del nodo encontrado en el anillo que lo contiene;
// retorna el tamaño del fichero si OK y -1 en caso de error;
// ESTA FUNCIÓN YA ESTA COMPLETADA
int ring_get_file(const char *filename, int hops) {
    
    if (!is_initialized()) return -1; // no está inicializada
    
    unsigned int ip, ip_local;
    unsigned short port, port_local;
    
    int res = ring_lookup(filename, hops, &ip, &port);
    
    ring_self(&ip_local, &port_local);
    
    // realiza la descarga si encontrado en un nodo que no es el local
    if ((res!=-1) && ((ip!=ip_local) || (port!=port_local)))
        res = ring_download(ip, port, filename);
    
    return res;
}

// funciones auxiliares
static int initialized;

static int initialize(void) {
    return initialized?1:(initialized=1,0);
}
static int is_initialized(void) {
    return initialized;
}
