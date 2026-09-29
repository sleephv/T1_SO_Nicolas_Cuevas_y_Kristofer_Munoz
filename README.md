# T1 - Planificador Dieciochero (Sistemas Operativos - UDP)

**Integrantes:**
- Nicolas Cuevas
- Kristofer Muñoz

Simulador en C++ para la planificación y ejecución concurrente de actividades de un asado dieciochero. El sistema modela las tareas mediante un Grafo Acíclico Dirigido (DAG) y gestiona su ejecución estricta a través de multiprocesamiento, respetando dependencias, límites de concurrencia y aplicando tolerancia a fallos mediante aislamiento de errores.

---

## 1. Funciones Implementadas y Requisitos Cumplidos

El sistema cumple con la totalidad de los requerimientos técnicos solicitados:

- **Parseo y Modelado (DAG):** Lectura robusta del archivo ignorando líneas vacías y limpiando espacios. Extrae ID, nombre, tiempo y dependencias; si el tiempo no se especifica, se asigna uno aleatorio entre 100 ms y 5000 ms. Las actividades se representan como un grafo en memoria con estados controlados (`PENDIENTE`, `EJECUTANDO`, `TERMINADO`, `ABORTADO`).
- **Multiprocesamiento Puro:** Creación de procesos utilizando estrictamente las llamadas al sistema `fork()` y `wait()`. Se cumple con la prohibición absoluta del uso de hilos (`threads`).
- **Control de Concurrencia (Límite K):** El proceso padre actúa como orquestador, contabilizando los procesos activos para asegurar que nunca se supere el límite máximo de $K$ tareas ejecutándose simultáneamente.
- **Comunicación IPC (Pipes):** Uso de tuberías anónimas unidireccionales creadas antes de cada `fork()`. Al finalizar, el hijo escribe un mensaje de éxito que el padre lee para propagar la finalización a las tareas dependientes.
- **Aislamiento de Errores (Fallo Simulado):** Cada proceso hijo posee un 15% de probabilidad de fallar internamente (`exit(1)`). El padre detecta esto y ejecuta una función recursiva que aborta en cadena únicamente la rama de tareas que dependía de la actividad fallida.
- **Manejo de Señales (Inspección Seremi):** Captura de la señal `SIGINT` (Ctrl+C). Al recibir la interrupción, el planificador envía la señal `SIGTERM` a todos los procesos hijos activos y limpia los procesos zombis antes de cerrarse limpiamente.

---

## 2. Justificación de Decisiones de Diseño (Fundamentos de SO)

Para asegurar la robustez del planificador y cumplir con los estándares de Sistemas Operativos, la arquitectura se basó en los siguientes principios teóricos:

*   **Creación de Procesos y Memoria Aislada:** Al utilizar la *syscall* `fork()`, el sistema operativo crea un proceso hijo que es una copia del padre, pero con espacios de memoria completamente independientes. Esto protege la integridad de las variables de cada tarea y cumple con la restricción de no usar memoria compartida mediante hilos. Toda la información vital de estos procesos es administrada por el OS a través de sus respectivos Process Control Blocks (PCB).

*   **Comunicación Inter-Procesos (IPC) mediante Pipes:** Dado que `fork()` aísla la memoria de los procesos, era imperativo implementar un mecanismo de comunicación para que los hijos notificaran al padre sobre la finalización de sus insumos. Se optó por el uso de `pipes` (tuberías anónimas), las cuales abren un canal de comunicación unidireccional (paso de mensajes) gestionado por el kernel.

*   **Control de Concurrencia sin Busy-Waiting:** Para controlar el límite $K$ de concurrencia, el proceso padre no se queda en un bucle infinito consumiendo ciclos de CPU preguntando si hay espacio (busy-waiting). En su lugar, cuando se alcanza el límite $K$, el bucle detiene la creación de nuevos procesos y el padre ejecuta la *syscall* `wait(&status)`. Esto cede el control al planificador (scheduler) del SO, suspendiendo al padre hasta que un hijo termine y lo despierte mediante una interrupción, logrando un uso eficiente del procesador.

*   **Prevención de Procesos Zombis y Huérfanos:** Cuando un proceso hijo termina, su entrada en la tabla de procesos (PCB) no se elimina automáticamente hasta que el padre recoja su estado de salida, generando un "proceso zombi". Nuestro motor principal utiliza `wait()` constantemente para recolectar estos estados. Además, en el manejador de la señal Seremi (`SIGINT`), tras asesinar a los hijos con `kill()`, se implementó un bucle `while(wait(NULL) > 0)` para limpiar exhaustivamente la tabla de procesos antes de finalizar el programa principal.

*   **Semillas de Aleatoriedad Seguras:** La clonación de procesos copia el estado exacto de la memoria, incluyendo las semillas del generador de números aleatorios. Para garantizar que la probabilidad de fallo del 15% sea estadísticamente independiente en cada hijo concurrente, se instanció `srand(time(NULL) ^ getpid())` exclusivamente dentro del flujo del proceso hijo, utilizando su ID único (`getpid()`).

---

## 3. Requisitos y Modo de Uso

### Entorno y Compilación
El proyecto está diseñado para entornos Linux/WSL y se compila utilizando el estándar estricto de C++17 mediante el `Makefile` incluido.

```bash
make
```
---
## 4. Ejecución Básica

El simulador requiere de dos parametros posicionales: el archivo con el plan de actividades y el límite de concurrencia de actividades $K$.

```bash
./planificador plan.txt $K$.
```
--- 

## 5. Pruebas de Estrés

Para validar el ítem de la rúbrica referente a la carga de trabajo masiva, se creó un script generador en Python `generador.py`. Este script permite crear un DAG complejo `plan_estres.txt`   con cientos o miles de actividades entrelazadas para poner a prueba el aislamiento de errores y el límite de concurrencia.

```bash
python3 generador.py
./planificador plan_estres.txt $K$
```
---
## 6. Limpieza de archivos

Para limpiar el directorio de los archivos binarios y objetos generados por el compilador, dentro del archivo `Makefile` se hizo que al hacer el comando:

```bash
make clean
```

Se realice una limpieza de los archivos.