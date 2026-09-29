#include <iostream>  // Para imprimir y mostrar errores en consola (cout, cerr)
#include <fstream>   // Para abrir y leer el archivo plan.txt (ifstream)
#include <sstream>   // Para cortar textos, súper útil para separar las líneas por ':' y ','
#include <vector>    // Para usar listas dinámicas (nuestra lista de actividades y PIDs)
#include <string>    // Para manejar texto de forma más fácil que los char* de C
#include <unistd.h>  // LIBRERÍA CLAVE DE SO: Trae fork(), pipe(), read(), write(), close() y usleep()
#include <sys/wait.h>// LIBRERÍA DE SO: Trae wait() y los macros (WIFEXITED) para revisar cómo murió el hijo
#include <cstring>   // Para limpiar la memoria sucia de los buffers con memset()
#include <csignal>   // LIBRERÍA DE SO: Para atrapar el Ctrl+C (SIGINT) y asesinar procesos (kill)
#include <algorithm> // Para usar la función remove() y borrar un PID específico de la lista global
#include <cstdlib>   // Para usar exit(), rand() y srand() (para la probabilidad del 15%)
#include <ctime>     // Para obtener la hora del sistema con time() y hacer que rand() sea realmente aleatorio

using namespace std;

// Variable global para que la señal de la SEREMI sepa qué procesos (PIDs) están corriendo y pueda matarlos.
vector<pid_t> pids_activos_global;

// --- MANEJO DE SEÑALES (Seremi) ---
// Función que se ejecuta automáticamente si el usuario presiona Ctrl+C (SIGINT).
void manejar_seremi(int sig) {
    (void)sig; // Silenciamos el warning de variable no usada
    cout << "\n\n[SEREMI] ¡Inspección de salubridad sorpresa (Ctrl+C) detectada!" << endl;
    cout << "[SEREMI] Abortando todos los procesos activos..." << endl;
    
    // Le mandamos la señal de terminar a cada hijo que siga vivo
    for (pid_t pid : pids_activos_global) {
        kill(pid, SIGTERM);
    }
    
    cout << "[SEREMI] Clausura completada. Limpiando procesos zombies..." << endl;
    // Hacemos wait para que el SO limpie la tabla de procesos y no queden "zombies"
    while(wait(NULL) > 0);
    
    cout << "[SEREMI] Saliendo del planificador limpiamente." << endl;
    exit(1);
}

// Estados posibles de cada tarea en nuestro grafo (DAG)
enum Estado { PENDIENTE, EJECUTANDO, TERMINADO, ABORTADO };

// --- ESTRUCTURA DEL GRAFO ---
// Representa un nodo del DAG con toda su info y herramientas de comunicación
struct Actividad {
    string id;
    string nombre;
    int tiempo_ms;
    vector<string> dependencias; 
    
    Estado estado = PENDIENTE;
    int dependencias_pendientes = 0; // Contador que bajará a medida que le avisen por pipe
    pid_t pid = -1; // Guarda el ID del proceso hijo asignado
    int pipe_fd[2]; // Descriptores del pipe: [0] lectura, [1] escritura
};

// --- AISLAMIENTO DE ERRORES (Recursivo) ---
// Si una tarea falla, esta función busca todas las tareas que dependían de ella y las cancela.
// Como las canceladas también pueden tener tareas que dependan de ellas, se llama a sí misma (recursión).
void abortar_rama(string id_fallida, vector<Actividad>& lista) {
    for (auto& act : lista) {
        if (act.estado == PENDIENTE) {
            // Revisamos si la tarea actual necesitaba la que acaba de fallar
            for (const string& dep : act.dependencias) {
                if (dep == id_fallida) {
                    act.estado = ABORTADO;
                    cout << "[AISLAMIENTO] Abortando: " << act.nombre 
                         << " (Dependía de actividad fallida: " << id_fallida << ")" << endl;
                    
                    // Efecto dominó: cancelamos a los que dependían de esta nueva cancelada
                    abortar_rama(act.id, lista);
                    break;
                }
            }
        }
    }
}

// --- FUNCIÓN PRINCIPAL ---
int main(int argc, char* argv[]) {
    // 1. Verificamos que nos pasen el archivo y el límite K por consola
    if (argc != 3) {
        cerr << "Uso: ./planificador <archivo.txt> <K_procesos_concurrentes>" << endl;
        return 1;
    }

    string archivo_nombre = argv[1];
    int limite_K = stoi(argv[2]);

    if (limite_K <= 0) {
        cerr << "Error: El límite K debe ser mayor a 0." << endl;
        return 1;
    }

    // 2. Parseo: Leer el archivo txt y armar nuestra lista de actividades
    ifstream archivo(archivo_nombre);
    if (!archivo.is_open()) {
        cerr << "Error al abrir el archivo: " << archivo_nombre << endl;
        return 1;
    }

    vector<Actividad> lista_actividades;
    string linea;
    srand(time(NULL)); // Semilla para generar tiempos y fallos aleatorios

    // Función lambda para limpiar espacios vacíos que a veces vienen en el txt
    auto limpiar_espacios = [](string& str) {
        if(str.empty()) return;
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == string::npos) {
            str = "";
            return;
        }
        size_t last = str.find_last_not_of(" \t\r\n");
        str = str.substr(first, (last - first + 1));
    };

    // Leemos línea por línea
    while (getline(archivo, linea)) {
        if (linea.empty()) continue;
        
        stringstream ss(linea);
        string id, nombre, tiempo_str, deps_str;
        
        // Separamos los datos usando los dos puntos ':' como delimitador
        getline(ss, id, ':');
        getline(ss, nombre, ':');
        getline(ss, tiempo_str, ':');
        getline(ss, deps_str); // Lo que sobra son las dependencias separadas por coma
        
        limpiar_espacios(id);
        limpiar_espacios(nombre);
        limpiar_espacios(tiempo_str);

        Actividad nueva_act;
        nueva_act.id = id;
        nueva_act.nombre = nombre;

        // Si el txt no trae tiempo, le inventamos uno entre 100 y 5000 ms
        if (tiempo_str.empty()) {
            nueva_act.tiempo_ms = 100 + rand() % 4901; 
        } else {
            nueva_act.tiempo_ms = stoi(tiempo_str);
        }

        // Procesamos la lista de dependencias separándolas por coma
        stringstream ss_deps(deps_str);
        string dep;
        while (getline(ss_deps, dep, ',')) {
            limpiar_espacios(dep);
            if (!dep.empty() && dep != "Ninguna") {
                nueva_act.dependencias.push_back(dep);
            }
        }
        
        // Guardamos cuántas tareas necesita terminar antes de poder lanzarse
        nueva_act.dependencias_pendientes = nueva_act.dependencias.size();
        lista_actividades.push_back(nueva_act);
    }
    archivo.close();

    // 3. Setup de la simulación
    signal(SIGINT, manejar_seremi); // Conectamos el Ctrl+C con nuestra función
    int procesos_activos = 0;
    int total_tareas = lista_actividades.size();

    cout << "\n--- INICIANDO PLANIFICADOR DIECIOCHERO (K=" << limite_K << ") ---" << endl;

    // 4. Bucle Principal (El "Motor")
    // Sigue dando vueltas hasta que todas las tareas se acaben (terminadas con éxito o abortadas)
    while (true) {
        int tareas_completadas = 0;
        for (const auto& act : lista_actividades) {
            if (act.estado == TERMINADO || act.estado == ABORTADO) {
                tareas_completadas++;
            }
        }
        if (tareas_completadas == total_tareas) break; // Criterio de término

        // Revisamos si podemos lanzar nuevas tareas
        for (auto& act : lista_actividades) {
            if (procesos_activos >= limite_K) break; // Respetar límite K de concurrencia

            // Tarea lista para ejecutarse: está pendiente y ya no debe dependencias
            if (act.estado == PENDIENTE && act.dependencias_pendientes == 0) {
                
                // Creamos el pipe antes de clonar el proceso
                if (pipe(act.pipe_fd) == -1) {
                    cerr << "Error al crear el pipe." << endl;
                    return 1;
                }

                pid_t pid = fork(); // Nace el proceso hijo

                if (pid == 0) {
                    // --- CÓDIGO DEL PROCESO HIJO ---
                    // Generamos nueva semilla para que la probabilidad sea independiente por hijo
                    srand(time(NULL) ^ getpid());
                    
                    close(act.pipe_fd[0]); // El hijo no va a leer, cerramos ese lado
                    cout << "[HIJO] Ejecutando: " << act.nombre << " (PID: " << getpid() << ") - " << act.tiempo_ms << "ms" << endl;
                    
                    usleep(act.tiempo_ms * 1000); // Simulamos el trabajo durmiendo el hilo
                    
                    // Ruleta rusa del fallo (15% de probabilidad)
                    int suerte = rand() % 100;
                    if (suerte < 15) {
                        cout << "[ERROR] Fallo crítico interno en: " << act.nombre << " (PID: " << getpid() << ")" << endl;
                        close(act.pipe_fd[1]);
                        exit(1); // Muere con error
                    }

                    // Si no falló, escribe el mensaje de éxito en el pipe
                    string mensaje = "¡Insumo de " + act.nombre + " listo!";
                    write(act.pipe_fd[1], mensaje.c_str(), mensaje.length() + 1);
                    close(act.pipe_fd[1]);
                    
                    exit(0); // Termina bien
                } 
                else if (pid > 0) {
                    // --- CÓDIGO DEL PROCESO PADRE ---
                    act.pid = pid;          
                    act.estado = EJECUTANDO; 
                    procesos_activos++;     
                    close(act.pipe_fd[1]); // El padre no va a escribir, cerramos ese lado
                    pids_activos_global.push_back(pid); // Lo anotamos por si cae la Seremi
                }
            }
        }

        // El padre atiende a los hijos que van terminando
        if (procesos_activos > 0) {
            int status;
            pid_t pid_terminado = wait(&status); // Se queda esperando hasta que 1 hijo muera

            if (pid_terminado > 0) {
                procesos_activos--;
                
                // Lo sacamos de la lista global porque ya murió natural
                pids_activos_global.erase(remove(pids_activos_global.begin(), pids_activos_global.end(), pid_terminado), pids_activos_global.end());

                // Buscamos cuál de todas las tareas fue la que acaba de terminar
                for (auto& act : lista_actividades) {
                    if (act.pid == pid_terminado) {
                        
                        // Si el hijo hizo exit(0) (Terminó bien)
                        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                            act.estado = TERMINADO;
                            
                            // Leemos el mensaje que nos dejó en el pipe
                            char buffer[256];
                            memset(buffer, 0, sizeof(buffer)); 
                            read(act.pipe_fd[0], buffer, sizeof(buffer));
                            close(act.pipe_fd[0]); 

                            cout << "[MENSAJE PIPE] " << buffer << endl;

                            // Avisamos a las tareas que estaban esperando este insumo
                            for (auto& dep_act : lista_actividades) {
                                if (dep_act.estado == PENDIENTE) {
                                    for (const string& dep : dep_act.dependencias) {
                                        if (dep == act.id) {
                                            dep_act.dependencias_pendientes--; // Restamos 1 dependencia pendiente
                                            break;
                                        }
                                    }
                                }
                            }
                        } 
                        // Si el hijo hizo exit(1) (Falló por el 15%)
                        else {
                            act.estado = ABORTADO;
                            close(act.pipe_fd[0]); // Cerramos lectura para no dejar basura
                            cout << "[PADRE] Detectado fallo en proceso " << act.nombre << ". Aislando errores..." << endl;
                            
                            // Llamamos a la función que bota toda la rama
                            abortar_rama(act.id, lista_actividades);
                        }
                        break; // Ya encontramos y procesamos al hijo, salimos del for
                    }
                }
            }
        }
    }
    
    cout << "--- SIMULACIÓN FINALIZADA ---" << endl;
    return 0;
}