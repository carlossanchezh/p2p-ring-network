// FUNCIONALIDAD DE LA PARTE SERVIDORA
#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <pthread.h>
#include "ring.h"
#include "common.h"

//Variables extern en el servidor para que peuda acceder a las variables del cliente
extern char shared_dir[1024]; //Directorio para guardar los ficheros descargados
extern unsigned int my_ip; // IP propia del nodo
extern unsigned short my_port; // Puerto propio del nodo
extern unsigned int succ_ip; //IP del sucesor
extern unsigned short succ_port; //Puerto del sucesor

//Declarar funcion cliente para poder usarla en servidor
int ring_remote_successor(unsigned int remote_ip, unsigned short remote_port, unsigned int *suc_ip, unsigned short *suc_port);

// funcion para el manejo de peticiones en el servidor
static void *request_handler(void *arg) {

    int cli_sock = (long)arg; //socket de cliente
    char op;

    if (recv(cli_sock, &op, 1, MSG_WAITALL) != 1) { // se recibe la operacion del cliente
        close(cli_sock);
        return NULL;
    }

    if (op == 'P') { //si la operacion solicitada por el cliente es P, el servidor responde con el PID
        
        int pid = getpid();
        int pid_net = htonl(pid); //formato de red
        send(cli_sock, &pid_net, sizeof(pid_net), 0);

    }else if(op == 'A'){

        //Recibir el puerto del nuevo nodo creado
        unsigned short new_port;
        if (recv(cli_sock, &new_port, sizeof(new_port), MSG_WAITALL)!= sizeof(new_port)){
            close(cli_sock);
            return NULL;
        }

        // Obtenr la IP del nuevo nodo 
        struct sockaddr_in cli_addr;
        socklen_t addr_len = sizeof(cli_addr);
        getpeername(cli_sock, (struct sockaddr*)&cli_addr, &addr_len);
        unsigned int new_ip = cli_addr.sin_addr.s_addr;

        //Guardar el antiguo sucesor para darselo al nuevo nodo creado
        unsigned int old_succ_ip = succ_ip;
        unsigned short old_succ_port = succ_port;

        //Actualizar la ip y port del sucesor para apuntar al nuevo nodo
        succ_ip = new_ip;
        succ_port = new_port;

        //Enviar el antiguo sucesor al nuevo nodo apra que actualice su sucesor 
        struct iovec iov[2];
        iov[0].iov_base = &old_succ_ip; //ip del antiguo sucesor 
        iov[0].iov_len  = sizeof(old_succ_ip);
        iov[1].iov_base = &old_succ_port; //puerto del antiguo sucesor
        iov[1].iov_len  = sizeof(old_succ_port);
        if (writev(cli_sock, iov, 2) != sizeof(old_succ_ip) + sizeof(old_succ_port)) {
            close(cli_sock);
            return NULL;
        }
        
    }else if(op == 'S'){

        //Enviar Ip y port del sucesor
        struct iovec iov[2];
        iov[0].iov_base = &succ_ip;
        iov[0].iov_len = sizeof(succ_ip);
        iov[1].iov_base = &succ_port;
        iov[1].iov_len = sizeof(succ_port);
        if (writev(cli_sock, iov, 2) != sizeof(succ_ip) + sizeof(succ_port)){
            close(cli_sock);
            return NULL;
        }
    
    }else if(op =='U'){

        unsigned int succ_of_succ_ip;
        unsigned short succ_of_succ_port;

        //Lamar a ring_remote_successor funcion cliente para obtener el sucesor 
        if (ring_remote_successor(succ_ip, succ_port, &succ_of_succ_ip, &succ_of_succ_port) == 0) {
            
            struct iovec iov[2];
            iov[0].iov_base = &succ_of_succ_ip; //ip del sucesor del sucesor
            iov[0].iov_len = sizeof(succ_of_succ_ip);
            iov[1].iov_base = &succ_of_succ_port; //puerto del sucesor del sucesor
            iov[1].iov_len = sizeof(succ_of_succ_port);
            writev(cli_sock, iov, 2);

        } else {
            //si error enviar 0 
            unsigned int zero_ip = 0;
            unsigned short zero_port = 0;
            struct iovec iov[2];
            iov[0].iov_base = &zero_ip;
            iov[0].iov_len = sizeof(zero_ip);
            iov[1].iov_base = &zero_port;
            iov[1].iov_len = sizeof(zero_port);
            writev(cli_sock, iov, 2);

        }

    }else if(op == 'D'){
        //Recibir informacion del fichero a descargar 
        
        //longitud del nombre del fichero
        int name_len_net;
        if (recv(cli_sock, &name_len_net, sizeof(name_len_net), MSG_WAITALL) != sizeof(name_len_net)) {
            close(cli_sock);
            return NULL;
        }
        int name_len = ntohl(name_len_net); //quitar formato de red 

        //Nombre del fichero 
        char *filename = malloc(name_len + 1); //memoria dinamica para guardar el nombre
        if (recv(cli_sock, filename, name_len, MSG_WAITALL) != name_len) {
            free(filename);
            close(cli_sock);
            return NULL;
        }
        filename[name_len] = '\0'; //añade fin al final

        //COnstruir ruta completa del fichero
        char filepath[1024];
        snprintf(filepath, sizeof(filepath), "%s/%s", shared_dir, filename);
        free(filename);//libera el nombre del fichero ya no se usa
        
        //Abrir el fichero
        int fd = open(filepath, O_RDONLY);
        if (fd < 0) { //si el fichero no existe enviar error
            
            int error_net = htonl(-1);
            send(cli_sock, &error_net, sizeof(error_net), 0);
            close(cli_sock);
            return NULL;
        }

        //Tamaño del fichero
        struct stat st;
        fstat(fd, &st);
        off_t file_size = st.st_size;
        int size_net = htonl(file_size);

        //Enviar tamaño del fichero
        if (send(cli_sock, &size_net, sizeof(size_net), MSG_MORE) != sizeof(size_net)) { //MSG_MORE para mandar el tamaño y contenido en el mismo paquete
            close(fd); //se cierra el descriptor si falla
            close(cli_sock);
            return NULL;
        }

        //Envair contenido del fichero con sendfile
        off_t offset = 0;
        ssize_t sent = sendfile(cli_sock, fd, &offset, file_size);
        if (sent != file_size) { //si no se puede enviar todo el fichero falla 
            close(fd);
            close(cli_sock);
            return NULL;
        }

        close(fd);//cierra el descriptor de ficehro ya no se usa 

    }else if(op == 'L'){
        
        //Recibir hops, longitud y nombre del cliente

        //Recibir hops
        int hops_net;
        if (recv(cli_sock, &hops_net, sizeof(hops_net), MSG_WAITALL) != sizeof(hops_net)) {
            close(cli_sock);
            return NULL;
        }

        int hops = ntohl(hops_net); //quitar formato de red

        //Recibir longitud 
        int name_len_net;
        if (recv(cli_sock, &name_len_net, sizeof(name_len_net), MSG_WAITALL) != sizeof(name_len_net)) {
            close(cli_sock);
            return NULL;
        }

        int name_len = ntohl(name_len_net); //quitar formato de red

        // Recibir nombre 
        char *filename = malloc(name_len + 1); //reservar memoria dinamica para el nombre
        if (recv(cli_sock, filename, name_len, MSG_WAITALL) != name_len) {
            free(filename);
            close(cli_sock);
            return NULL;
        }

        filename[name_len] = '\0';//añadir fin

        //Ruta completa del fichero
        char filepath[1024];
        snprintf(filepath, sizeof(filepath), "%s/%s", shared_dir, filename);

        //Comprobarsi este nodo tiene el fichero bucado
        int fd = open(filepath, O_RDONLY);
        
        //Variables para informar al cliente que no se encontro el ficehro
        unsigned int zero_ip = 0;
        unsigned short zero_port = 0;

        if (fd >= 0) { // Si el fichero está en este nodo
        
            close(fd);
            
            // Responder con mi IP y puerto
            struct iovec iov[2];
            iov[0].iov_base = &my_ip;
            iov[0].iov_len = sizeof(my_ip);
            iov[1].iov_base = &my_port;
            iov[1].iov_len = sizeof(my_port);
            if(writev(cli_sock, iov, 2) != sizeof(my_ip) + sizeof(my_port)){
                close(cli_sock);
                return NULL;
            }
        
        }else if (hops == 0) { // Si no hay más saltos y no encontró el fichero
        
            //Se notifica al qcliente que no se encontro el fichero
            struct iovec iov[2];
            iov[0].iov_base = &zero_ip;
            iov[0].iov_len = sizeof(zero_ip);
            iov[1].iov_base = &zero_port;
            iov[1].iov_len = sizeof(zero_port);
            if(writev(cli_sock, iov, 2) != sizeof(zero_ip) + sizeof(zero_port)){
                 close(cli_sock);
                return NULL;
            }
        
        }else {
            
            // Enviar op L al sucesor para iniciar la busqueda en el sisguiente nodo con hops -1
            int succ_sock = create_socket_cln(succ_ip, succ_port); //sock para conectar con el sucesor
            
            if (succ_sock < 0) { //si no puede conectar con el sucesor informa al cliente que no se encontro el fichero
                
                struct iovec iov[2];
                iov[0].iov_base = &zero_ip;   
                iov[0].iov_len = sizeof(zero_ip);
                iov[1].iov_base = &zero_port; 
                iov[1].iov_len = sizeof(zero_port);
                if(writev(cli_sock, iov, 2) != sizeof(zero_ip) + sizeof(zero_port)){
                    close(cli_sock);
                    return NULL;
                }

                free(filename);
                close(cli_sock);
                return NULL;
            }

            //Enviar al sucesor op, hops -1, longitud y nombre
            char op_succ = 'L';
            int hops_succ = htonl(hops - 1);
            int name_len_net_succ = htonl(name_len);

            struct iovec iov2[4];
            iov2[0].iov_base = &op_succ; //op       
            iov2[0].iov_len = 1;
            iov2[1].iov_base = &hops_succ; //hops    
            iov2[1].iov_len = sizeof(hops_succ);
            iov2[2].iov_base = &name_len_net_succ; // longitud
            iov2[2].iov_len = sizeof(name_len_net_succ);
            iov2[3].iov_base = filename; // nombre      
            iov2[3].iov_len = name_len;
            if(writev(succ_sock, iov2, 4) != 1 + sizeof(hops_succ) + sizeof(name_len_net_succ) + name_len){
                close(cli_sock);
                return NULL;
            }

            //Recibir respuesta del sucesor y enviarla al cliente
            unsigned int found_ip;
            unsigned short found_port;

            if(recv(succ_sock, &found_ip, sizeof(found_ip), MSG_WAITALL)!= sizeof(found_ip)){ //Si no se puede recibir la ip
                //Notificar al cliente que no se ha podido encontrar el ficheo
                close(succ_sock);
                struct iovec iov[2];
                iov[0].iov_base = &zero_ip;
                iov[0].iov_len = sizeof(zero_ip);
                iov[1].iov_base = &zero_port;
                iov[1].iov_len = sizeof(zero_port);
                if(writev(cli_sock, iov, 2) != sizeof(zero_ip) + sizeof(zero_port)){
                    close(cli_sock);
                    return NULL;
                }
                free(filename);
                return NULL;
            }
            
            if(recv(succ_sock, &found_port, sizeof(found_port), MSG_WAITALL) != sizeof(found_port) ){ //Si no se puede recibir el puerto
                //Notificar al cliente que no se ha podido encontrar el ficheo
                close(succ_sock);
                struct iovec iov[2];
                iov[0].iov_base = &zero_ip;
                iov[0].iov_len = sizeof(zero_ip);
                iov[1].iov_base = &zero_port;
                iov[1].iov_len = sizeof(zero_port);
                if(writev(cli_sock, iov, 2) != sizeof(zero_ip) + sizeof(zero_port)){
                    close(cli_sock);
                    return NULL;
                }
                free(filename);
                return NULL;
            }

            close(succ_sock);

            struct iovec iov[2];
            iov[0].iov_base = &found_ip;   
            iov[0].iov_len = sizeof(found_ip);
            iov[1].iov_base = &found_port; 
            iov[1].iov_len = sizeof(found_port);
            if(writev(cli_sock, iov, 2) != sizeof(found_ip)+sizeof(found_port)){
                close(cli_sock);
                return NULL;
            }

        }

        free(filename); //libera memoria dinamica ya no se va a usar
    }

    close(cli_sock); //se cierra el socket al terminar de tratar la peticion

    return NULL;

}

// función para el thread que implementa la funcionalidad de servidor
// debe recibir como argumento el socket de servicio
void *server_thread(void *arg){

    int srv_sock = (long)arg;

    //Bucle infinito del servidor para aceptar conexiones
    while(1){
        struct sockaddr_in cli_addr;
        socklen_t addr_len = sizeof(cli_addr);
        
        int cli_sock = accept(srv_sock, (struct sockaddr*)&cli_addr, &addr_len);

        if(cli_sock < 0) {
            perror("accept");
            continue;
        }

        if(create_thread(request_handler, (void*)(long)cli_sock) != 0){ //crea un hilo para atender cada coenxion creada
            perror("Error creando el thread para la peticion");
            close(cli_sock);
        } 

    }

    return NULL;
}
