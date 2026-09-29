# T1 - Planificador Dieciochero (Sistemas Operativos - UDP)

**Integrantes:**
- Nicolas Cuevas
- Kristofer Muñoz

Simulador en C++ para la planificación y ejecución concurrente de actividades de un asado dieciochero. El sistema modela las tareas mediante un Grafo Acíclico Dirigido (DAG) y gestiona su ejecución estricta a través de multiprocesamiento, respetando dependencias, límites de concurrencia y aplicando tolerancia a fallos.

---

## 1. Funciones Implementadas y Requisitos Cumplidos

El sistema cumple con la totalidad de los requerimientos técnicos solicitados en la rúbrica:

- **Parseo y Modelado (DAG):** Lectura robusta de `plan.txt` ignorando líneas vacías y limpiando espacios. Extrae ID, nombre, tiempo y dependencias. Si el tiempo de ejecución no se especifica, el sistema autogenera uno aleatorio entre 100 ms y 5000 ms. Las actividades se representan en memoria como un grafo de dependencias con estados controlados (`PENDIENTE`, `EJECUTANDO`, `TERMINADO`, `ABORTADO`).
- **Multiprocesamiento Puro:** Creación de procesos utilizando estrictamente las llamadas al sistema `fork()` y `wait()`. No se recurre al uso de hilos (`threads`).
- **Control de Concurrencia (Límite K):** El proceso padre actúa como orquestador, contabilizando los procesos activos para asegurar que **nunca** se supere el límite máximo de $K$ tareas ejecutándose simultáneamente.
- **Comunicación IPC (Pipes):** Uso de tuberías unidireccionales (tuberías anónimas / `pipe()`) creadas antes de cada `fork()`. Al finalizar, cada proceso hijo escribe un mensaje de éxito ("¡Insumo de [Actividad] listo!") que el padre lee para propagar la finalización a las tareas dependientes.
- **Aislamiento de Errores (Fallo Simulado):** Cada proceso hijo posee un 15% de probabilidad de fallar internamente y devolver un código de error (`exit(1)`). El proceso padre detecta esta anomalía y ejecuta una función recursiva que **aborta en cadena** únicamente la rama de tareas que dependía de la actividad fallida, permitiendo que el resto del grafo finalice su ejecución.
- **Manejo de Señales (Inspección Seremi):** Captura de la señal `SIGINT` (Ctrl+C) a través de `<csignal>`. Al recibir la interrupción, el planificador aborta el plan, envía la señal `SIGTERM` a todos los procesos hijos activos en su lista global y limpia los procesos zombies mediante `wait(NULL)` antes de cerrarse limpiamente.

---

## 2. Requisitos del Sistema

- Entorno Linux o WSL (Windows Subsystem for Linux).
- Compilador `g++` con soporte para el estándar C++17.
- Herramienta `make`.

---

## 3. Modo de Uso

### Compilación Estricta Automática
El proyecto incluye un archivo `Makefile` configurado con las banderas estrictas exigidas. Para compilar, simplemente abre la terminal en el directorio del proyecto y ejecuta:
```bash
make
