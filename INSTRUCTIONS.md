# Instrucciones de instalación y ejecución

## Requisitos

- **Linux**, **MacOS** o **WSL** (Windows Subsystem for Linux)

## Instalación

### 1. Clonar el repositorio

```bash
git clone https://github.com/carlossanchezh/p2p-ring-network.git
```

> Si no usas Git, descarga el `.zip` del proyecto y descomprímelo en una carpeta local.

### 2. Comprobar tener las herramientas necesarias 

| Herramienta | Uso | Cómo comprobar |
|---|---|---|
| `gcc` | Compilador de C | `gcc --version` |
| `make` | Sistema de compilación | `make --version` |
| `pthread` | Librería de threads (viene con glibc) | — |

## Ejecución de la Red

### 1. Compilar 

Desde el directorio `/src` compilar con `make`

```bash
cd p2p-ring-network/src
```

```bash
make
```

Esto genera un ejecutable `ring` en el mismo directorio

### 2. Ejecutar el primer nodo de la red

El primer nodo crea la red. Solo necesita el directorio compartido `<shared_dir>`, que podrá contener los archivos que el nodo comparte con los demás.

1. Si no está creado el directorio compartido, créalo:

```bash
mkdir <shared_dir>
```

> `<shared_dir>` corresponde al nombre de un directorio cualquiera, por ejemplo, `dir1`

2. Ejecutar el primer nodo:

```bash
./ring <shared_dir>
```

3. La salida esperada será: 

```plaintext
Bienvenido a la red P2P RING
----------------------------
El equipo <hostname> con IP <ip> y puerto <puerto> (PID <pid>) es el primer nodo de una nueva red

Seleccione operación (...)
```

4. Apunta la **IP** `<ip>` y el **puerto** `<puerto>` que aparecen en el mensaje. Los necesitarás para que otros nodos se unan a la red.

### 3. Ejecutar un nodo adicional

Un nodo que quiere unirse a una red existente necesita, además de su directorio compartido `<shared_dir>`, la IP `<remote_ip>` y el puerto `<remote_port>` de un nodo ya conectado.

> Cada nodo se ejecuta en su propia terminal (o en su propia máquina). El proceso `ring` no termina mientras estás en el menú, así que para tener varios nodos activos a la vez necesitas varias terminales abiertas simultáneamente.
>
> Si el nuevo nodo está en una red diferente a la que se quiere conectar, asegúrate de que su IP es alcanzable.

1. Si no está creado el directorio compartido, créalo:

```bash
mkdir <shared_dir>
```

> Este `<shared_dir>` es diferente al del nodo inicial se creará otro, por ejemplo, `dir2`

2. Ejecutar el nodo adicional:

```bash
./ring <shared_dir> <remote_ip> <remote_port>
```

3. La salida esperada será: 

```plaintext
El equipo <hostname> con IP <ip> y puerto <puerto> (PID <pid>) se incorpora a la red
a través del equipo <remote_ip> puerto <remote_port>
```

> Siguiendo estas instrucciones se pueden ejecutar cuantos nodos se requieran para la red.

## Ejecución de las operaciones del sistema

| Tecla | Operación | Argumentos que pide |
|-------|-----------|---------------------|
| `I` | Info del nodo local | — |
| `P` | PID de un nodo remoto | IP + puerto |
| `S` | Sucesor local | — |
| `R` | Sucesor de un nodo remoto | IP + puerto |
| `U` | Sucesor del sucesor de un nodo remoto | IP + puerto |
| `D` | Descarga directa de un fichero | IP + puerto + nombre |
| `L` | Búsqueda en el anillo | Nombre + hops |
| `G` | Buscar y descargar | Nombre + hops |

> El parámetro `hops` es el número máximo de nodos que se visitan contando desde el tuyo. `hops=0` = solo el nodo local; `hops=1` = local + sucesor; y así.

### Ejemplos de uso

Escenario de referencia para todos los ejemplos:

- Nodo 1 → 127.0.0.1:44017 (primer nodo, crea la red)

- Nodo 2 → 127.0.0.1:34093

- Nodo 3 → 127.0.0.1:33007

- Nodo 4 → 127.0.0.1:44535 (contiene f.txt)

Anillo: 1 → 3 → 2 → 4 → 1

> Como se puede comprobar, todos los nodos se están ejecutando desde la misma máquina, cada uno en una terminal distinta.

#### `I` — Info del nodo local

**Correcto:** Muestra la IP y el puerto del nodo en el que estás. No pide argumentos.

```plaintext
I

IP 127.0.0.1 port 44017
```

#### `P` — PID de un nodo remoto

**Correcto:** Pide la IP y el puerto del nodo remoto. Devuelve su PID. Útil para comprobar que hay comunicación.

```plaintext
P
Introduzca el nombre o la IP del host remoto: 127.0.0.1
Introduzca el puerto del host remoto: 34093

PID 20020
```

**Error:** El nodo remoto no está escuchando o la IP/puerto no son correctos:

```plaintext
P
Introduzca el nombre o la IP del host remoto: 127.0.0.1
Introduzca el puerto del host remoto: 99999

error en ring_remote_pid
```

#### `S` — Sucesor local

**Correcto:** Muestra la IP y el puerto del sucesor del nodo en el que estás. No pide argumentos.

```plaintext
S

IP 127.0.0.1 port 33007
```

> Desde el nodo 1, su sucesor es el nodo 3.

#### `R` — Sucesor de un nodo remoto

**Correcto:** Pide la IP y el puerto de un nodo remoto. Muestra quién es su sucesor.

```plaintext
R
Introduzca el nombre o la IP del host remoto: 127.0.0.1
Introduzca el puerto del host remoto: 34093

IP 127.0.0.1 port 44535
```

> Desde cualquier nodo, el sucesor del nodo 2 es el nodo 4.

**Error:** No se puede contactar con el nodo remoto:

```plaintext
R
Introduzca el nombre o la IP del host remoto: 127.0.0.1
Introduzca el puerto del host remoto: 99999

error en ring_remote_successor
```

#### `U` — Sucesor del sucesor de un nodo remoto

**Correcto:** Pide la IP y el puerto de un nodo remoto. Muestra quién es el sucesor de su sucesor.

```plaintext
U
Introduzca el nombre o la IP del host remoto: 127.0.0.1
Introduzca el puerto del host remoto: 44017

IP 127.0.0.1 port 34093
```

> Desde cualquier nodo: el sucesor del nodo 1 es el 3, y el sucesor del 3 es el 2.

**Error:** No se puede contactar con el nodo remoto o con su sucesor:

```plaintext
U
Introduzca el nombre o la IP del host remoto: 127.0.0.1
Introduzca el puerto del host remoto: 99999

error en ring_remote_successor_successor
```

#### `D` — Descarga directa de un fichero

**Correcto:** Pide la IP y el puerto de un nodo remoto, y el nombre del fichero. Lo descarga a tu `shared_dir` sin pasar por el anillo.

```plaintext
D
Introduzca el nombre o la IP del host remoto: 127.0.0.1
Introduzca el puerto del host remoto: 44535
Introduzca el nombre del fichero: f.txt
```

**Error:** Si el fichero no existe en el nodo indicado:

```plaintext
D
Introduzca el nombre o la IP del host remoto: 127.0.0.1
Introduzca el puerto del host remoto: 44535
Introduzca el nombre del fichero: noexiste.txt

error en ring_download
```

**Error:** No se puede contactar con el nodo remoto:

```plaintext
D
Introduzca el nombre o la IP del host remoto: 127.0.0.1
Introduzca el puerto del host remoto: 99999
Introduzca el nombre del fichero: f.txt

error en ring_download
```

#### `L` — Búsqueda en el anillo

**Correcto:** Pide el nombre del fichero y el número máximo de saltos (hops). Recorre el anillo desde tu nodo hasta encontrarlo o agotar los saltos. Devuelve la IP y el puerto del nodo que lo contiene.

```plaintext
L
Introduzca el nombre del fichero: f.txt
Introduzca el número máximo de nodos visitados: 4

IP 127.0.0.1 port 44535
```

**Error:** Búsqueda con pocos saltos (no llega al nodo 4):

```plaintext
L
Introduzca el nombre del fichero: f.txt
Introduzca el número máximo de nodos visitados: 2

error en ring_lookup
```

**Error:** Fichero inexistente:

```plaintext
L
Introduzca el nombre del fichero: noexiste.txt
Introduzca el número máximo de nodos visitados: 4

error en ring_lookup
```

**Error:** Búsqueda restringida al nodo local (hops = 0) de un fichero que no está en él:

```plaintext
L
Introduzca el nombre del fichero: f.txt
Introduzca el número máximo de nodos visitados: 0

error en ring_lookup
```

#### `G` — Buscar y descargar

**Correcto:** Combina `L` y `D`: busca el fichero en el anillo y, si lo encuentra en otro nodo, lo descarga. Pide el nombre y los hops.

```plaintext
G
Introduzca el nombre del fichero: f.txt
Introduzca el número máximo de nodos visitados: 4
```

**Error:** Pocos saltos (no se encuentra el fichero):

```plaintext
G
Introduzca el nombre del fichero: f.txt
Introduzca el número máximo de nodos visitados: 2

error en ring_get_file
```

**Error:** El fichero no existe en ningún nodo del anillo:

```plaintext
G
Introduzca el nombre del fichero: noexiste.txt
Introduzca el número máximo de nodos visitados: 4

error en ring_get_file
```
