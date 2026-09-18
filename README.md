# T1 - Planificador Dieciochero (Sistemas Operativos - UDP)

**Integrantes:**
- Nicolas Cuevas
- Kristofer Muñoz

---

## 1. Funciones Implementadas
- **Parseo (`parse_plan`)**: Lee `plan.txt` ignorando líneas vacías, extrayendo ID, nombre, tiempo (si viene vacío, asigna aleatorio entre 100 y 5000 ms) y dependencias.
- **Modelado DAG**: Representa las actividades como un grafo acíclico dirigido en memoria (listas/vectores de adyacencia).
- **Control de Concurrencia**: Semaforización / control de procesos activos para no superar nunca $K$ procesos simultáneos.
- **Pipes (IPC)**: Creación de tuberías anónimas por nodo/arista para propagar insumos al finalizar una actividad dependiente.
- **Aislamiento de errores / Señales**: Manejo de `SIGINT` (Ctrl+C) para abortar todo el árbol de procesos, e isolación por rama en caso de fallo interno de un proceso hijo.

## 2. Modo de Uso

### Compilación estricta
```bash
# En C++
g++ -Wall -Wextra -std=c++17 -lpthread planificador.cpp -o planificador

# Si usas C
gcc -Wall -Wextra -std=c17 -lpthread planificador.c -o planificador
