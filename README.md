# P2P RING Network

## Descripción

Red P2P estructurada con topología en anillo para compartir ficheros, implementada en C
sobre sockets TCP. Cada nodo actúa simultáneamente como cliente y servidor,
formando un anillo en el que la búsqueda de ficheros se realiza de forma
lineal recorriendo los sucesores hasta encontrar el recurso o agotar el número máximo de saltos.

El sistema permite:

- **Incorporarse a una red existente** indicando un nodo de contacto.

- **Compartir un directorio local** con ficheros accesibles al resto de nodos.

- **Buscar un fichero** en el anillo con un número máximo de saltos.

- **Descargar un fichero** directamente desde el nodo que lo contiene.

- **Buscar y descargar** un fichero en una sola operación.

Cada nodo de la red ejecuta la misma aplicación (`ring`) y tiene una doble
funcionalidad:

- **Cliente**: atiende al usuario local y encamina sus peticiones.

- **Servidor**: procesa las peticiones que llegan desde otros nodos.

Ambas partes conviven en el mismo proceso.

## Arquitectura

### Diseño de la Red

El sistema está diseñado como una red P2P estructurada en anillo, sin ningún nodo central ni jerarquía. Cada nodo es autónomo y mantiene únicamente el estado mínimo necesario para participar en el anillo.

#### Estado por nodo

Cada nodo almacena exclusivamente:

- Su propia IP y su propio puerto de servicio (`my_ip`, `my_port`).

- La IP y el puerto de su sucesor en el anillo (`succ_ip`, `succ_port`).

- El directorio compartido del que exporta ficheros (`shared_dir`).

No existe una tabla global ni un registro de todos los nodos, el conocimiento del anillo es local y distribuido. Un nodo solo sabe quién es su sucesor; para llegar más lejos, delega en él.

Cada proceso `ring` tiene doble rol y actúa simultáneamente como:

- **Cliente**, cuando el usuario local pide una operación que implica contactar con otros nodos.

- **Servidor**, cuando llega una petición desde otro nodo.

#### Topología

La topología es un anillo dirigido donde cada nodo apunta a un único sucesor. Como el anillo es cerrado, partiendo de cualquier nodo y siguiendo sucesores se recorren todos los nodos exactamente una vez. Esta estructura es la que permite la búsqueda lineal de ficheros.

#### Concurrencia

El servidor sigue el modelo thread-per-request:

- Un único thread servidor ejecuta `server_thread()` en `ring_srv.c` y se queda en un bucle llamando a `accept()`.

- Por cada conexión aceptada, se lanza un thread nuevo `request_handler()` que atiende una única petición y termina.

- Los threads se crean en modo detached mediante `create_thread()` en `common.c`, de modo que liberan sus recursos automáticamente al acabar.

- No se mantienen conexiones persistentes: se usa una conexión **TCP** por petición (abrir, enviar, recibir, cerrar).

#### Formato de red

Todas las IPs y puertos que circulan por la API viajan en formato de red (`htonl()` / `htons()`). La conversión desde/hacia formato de host se realiza únicamente en los puntos de entrada/salida, es decir, en `main.c` cuando se leen los datos del usuario o se imprimen resultados.

#### Envío de múltiples campos sin fragmentar:

Muchas operaciones requieren enviar varios elementos seguidos por el mismo socket. Para evitar la fragmentación y el envío de varios paquetes TCP se usan dos técnicas:

- `writev()`: agrupa varios buffers en una sola llamada al sistema, que los envía de forma atómica.

- `MSG_MORE`: en `send()`, indica al sistema operativo que aún quedan más datos por enviar en esa misma conexión, de modo que no cierre el paquete TCP hasta el último `send()`.

#### Objetos de tamaño variable

Los nombres de fichero y el contenido de los ficheros tienen tamaño variable. Por eso, antes de enviarlos, siempre se transmite su longitud (en formato de red) como un entero. En recepción, se reserva memoria dinámica del tamaño recibido y, en el caso de strings, se añade el carácter nulo. Esto elimina cualquier límite artificial de tamaño y permite usar buffers del tamaño justo.

### Componentes principales

El proyecto se organiza en seis ficheros fuente (más un Makefile), cada uno con una responsabilidad bien delimitada. La separación entre cliente, servidor y utilidades comunes permite que ambos roles compartan la lógica de red sin duplicar código.

#### `main.c` — Interfaz de usuario

Es el punto de entrada del programa, permite poder compilar y ejecutar el proyecto.

- Valida los argumentos de línea de comandos (`shared_dir` y, opcionalmente, host y puerto del nodo de contacto).

- Traduce nombres de host a direcciones IP con `getaddrinfo()` y convierte puertos a formato de red con `htons()`.

- Llama a `ring_init()` para arrancar el nodo.

- Muestra el menú de operaciones y, en un bucle, lee la opción del usuario y llama a la función correspondiente de la API.

- Convierte los resultados de formato de red a formato de host (`ntohs()`, `inet_ntoa()`) para imprimirlos.

#### `ring.h` — API de la aplicación

Declara las funciones que `main.c` puede invocar sobre la parte cliente, es imprescindible para compilar `main.c` contra la implementación.

Define, entre otras:

- `ring_init()`, `ring_self()`, `ring_successor()`

- `ring_remote_pid()`, `ring_remote_successor()`, `ring_remote_successor_successor()`

- `ring_download()`, `ring_lookup()`, `ring_get_file()`

Todas las IPs y puertos que aparecen en las firmas están en formato de red.

#### `ring_cln.c` — Parte cliente

Implementa toda la lógica de la parte cliente del nodo: la que se ejecuta cuando el usuario local pide una operación y hay que contactar con otros nodos.

- Mantiene el estado local del nodo: `my_ip`, `my_port`, `succ_ip`, `succ_port` y `shared_dir`.

- Implementa las funciones declaradas en `ring.h`.

- Al arrancar (`ring_init()`), crea el socket servidor, lanza el thread servidor y, si procede, se une a una red existente enviando la operación de alta al nodo de contacto.

- Cada operación remota abre una conexión TCP con `create_socket_cln()`, envía el código de operación (junto con sus parámetros si los tiene), espera la respuesta y cierra la conexión.

- En `ring_download()` recibe el fichero con `mmap()` sobre el fichero destino.

- En `ring_get_file()` combina `ring_lookup()` y `ring_download()` para buscar y descargar en una sola llamada.

#### `ring_srv.c` — Parte servidor

Implementa la parte servidora del nodo: la que responde a las peticiones que llegan desde otros nodos.

- Define `server_thread()`, que crea el socket servidor y queda en un bucle llamando a `accept()`.

- Por cada conexión aceptada, lanza un thread `request_handler()` con `create_thread()`, `request_handler()` lee el código de operación y ejecuta la lógica correspondiente:

  - `P`: responde con el PID del proceso.

  - `A`: da de alta un nuevo nodo, actualizando su sucesor.

  - `S`: responde con su sucesor.

  - `U`: consulta a su sucesor (actuando como cliente) y reenvía la respuesta.

  - `D`: envía un fichero con `sendfile()` (o un tamaño -1 si no existe).

  - `L`: busca en el anillo (local o reenviando al sucesor con hops - 1).

- Cada thread atiende una única petición y termina (es detached).

#### `common.c` — Funcionalidad común

Reúne las utilidades que usan tanto el cliente como el servidor, evitando duplicar código.

- `create_socket_srv()`: crea el socket servidor, lo asocia a un puerto elegido por el SO, activa `SO_REUSEADDR`, lo pone en modo escucha y devuelve el puerto asignado en formato de red.

- `create_socket_cln()`: crea un socket TCP y se conecta al nodo remoto identificado por IP y puerto (ambos en formato de red).

- `create_thread()`: crea un thread en modo detached con `pthread_create()`.

#### `common.h` — Interfaz de la funcionalidad común

Declara las funciones implementadas en `common.c` para que puedan ser usadas desde `ring_cln.c` y `ring_srv.c`.

#### `Makefile` — Reglas de compilación

Compila `main.c`, `ring_cln.c`, `ring_srv.c` y `common.c`, genera los `.o` correspondientes y los enlaza en un único ejecutable `ring`, enlazando además la librería `pthread`.

### Funcionamiento

Las operaciones se identifican mediante un **código de operación** de un
carácter. La siguiente tabla resume las operaciones soportadas por el
servidor:

| Código | Operación | Parámetros | Respuesta |
|--------|-----------|------------|-----------|
| `P` | Obtener PID del nodo remoto | — | PID |
| `A` | Alta de un nuevo nodo | Puerto del nuevo nodo | IP y puerto del antiguo sucesor |
| `S` | Obtener sucesor | — | IP y puerto del sucesor |
| `U` | Obtener sucesor del sucesor | — | IP y puerto del sucesor del sucesor |
| `D` | Descarga directa de fichero | Longitud + nombre del fichero | Tamaño + contenido |
| `L` | Lookup en el anillo | Hops + longitud + nombre | IP y puerto del nodo que lo tiene |

A continuación se detallan estas operaciones indicando funciones y archivos involucrados en cada una de ellas:

**Operación** `P` — Obtener PID

- **Cliente**: `ring_remote_pid()` en `ring_cln.c` se conecta al nodo remoto con `create_socket_cln()` en (`common.c`), envía el código `P` y espera un entero.

- **Servidor**: `request_handler()` en `ring_srv.c` lee `P`, llama a `getpid()` y envía el PID en formato de red.

- **Respuesta**: PID del nodo remoto.

**Operación** `A` — Alta de un nuevo nodo

- **Cliente**: `ring_init()` en `ring_cln.c`. Cuando se arranca con un nodo de contacto, se conecta a él con `create_socket_cln()` en (`common.c`) y le envía `A` seguido de su propio puerto.

- **Servidor**: `request_handler()` en `ring_srv.c` lee `A`, obtiene la IP del nuevo nodo con `getpeername()`, guarda el sucesor antiguo y actualiza su sucesor apuntando al nuevo nodo.

- **Respuesta**: IP y puerto del antiguo sucesor, que el nuevo nodo guarda como su sucesor.

**Operación** `S` — Obtener sucesor

- **Cliente**: `ring_remote_successor()` en `ring_cln.c`. Envía `S` y espera IP y puerto.

- **Servidor**: `request_handler()` en `ring_srv.c` responde con `succ_ip` y `succ_port`.

- **Respuesta**: IP y puerto del sucesor del nodo remoto.

**Operación** `U` — Sucesor del sucesor

- **Cliente**: `ring_remote_successor_successor()` en `ring_cln.c`. Envía `U`.

- **Servidor**: `request_handler()` en `ring_srv.c`. Aquí el servidor actúa como cliente: llama a `ring_remote_successor()` sobre su propio sucesor y reenvía la respuesta al solicitante.

- **Respuesta**: IP y puerto del sucesor del sucesor.

**Operación** `D` — Descarga directa

- **Cliente**: `ring_download()` en `ring_cln.c`. Envía `D`, la longitud del nombre y el nombre del fichero. Recibe el tamaño y el contenido, que escribe usando `mmap()` sobre el fichero destino.

- **Servidor**: `request_handler()` en `ring_srv.c`. Comprueba que el fichero existe en `shared_dir`. Si existe, envía el tamaño con `send()` y el contenido con `sendfile()`. Si no, envía tamaño -1 como error.

- **Respuesta**: tamaño del fichero (o -1) seguido del contenido.

**Operación** `L` — Búsqueda en el anillo

- **Cliente**: `ring_lookup()` en `ring_cln.c`. Envía `L`, el número de saltos y el nombre del fichero. Se conecta al nodo local para iniciar la búsqueda de forma uniforme.

- **Servidor**: `request_handler()` en `ring_srv.c`. Comprueba si el fichero está en su `shared_dir`:

    - Si está, responde con su propia IP y puerto.

    - Si no está y hops == 0, responde con (0, 0) indicando "no encontrado".

    - Si no está y quedan saltos, actúa como cliente: se conecta a su sucesor, le reenvía la misma operación con hops - 1 y devuelve al solicitante la respuesta que reciba.

- **Respuesta**: IP y puerto del nodo que contiene el fichero, o (0, 0) si no se encuentra.

### Tecnologías

- Lenguaje: **C**
- Comunicación entre nodos: **Sockets TCP**

## Estructura del proyecto

```plaintext
.
├── src/
│   ├── include/
│   │   ├── common.h       # Interfaz de la funcionalidad común
│   │   └── ring.h         # API ofrecida a las aplicaciones 
│   │
│   ├── Makefile           # Reglas de compilación
│   ├── common.c           # Funcionalidad común (sockets, threads)
│   ├── main.c             # Interfaz de usuario (menú de texto)
│   ├── ring_cln.c         # Parte cliente de la aplicación
│   └── ring_srv.c         # Parte servidor de la aplicación
│ 
└──  README.md             # Descripción del proyecto 
```
