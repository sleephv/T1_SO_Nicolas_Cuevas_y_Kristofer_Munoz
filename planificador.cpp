#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <unistd.h>
#include <sys/wait.h>
#include <cstring>
#include <csignal>
#include <algorithm>
#include <cstdlib> // Para rand() y srand()
#include <ctime>   // Para time()

using namespace std;

// Variable global para que la señal SEREMI pueda acceder a los hijos vivos
vector<pid_t> pids_activos_global;

// --- MANEJO DE SEÑALES (Seremi) ---
void manejar_seremi(int sig) {
    (void)sig; // Evita el warning del compilador por parámetro sin usar
    cout << "\n\n[SEREMI] ¡Inspección de salubridad sorpresa (Ctrl+C) detectada!" << endl;
    cout << "[SEREMI] Abortando todos los procesos activos..." << endl;
    
    // Matar a todos los hijos registrados
    for (pid_t pid : pids_activos_global) {
        kill(pid, SIGTERM);
    }
    
    cout << "[SEREMI] Clausura completada. Limpiando procesos zombies..." << endl;
    // Evitar que los hijos asesinados queden como zombies en el sistema operativo
    while(wait(NULL) > 0);
    
    cout << "[SEREMI] Saliendo del planificador limpiamente." << endl;
    exit(1);
}

enum Estado { PENDIENTE, EJECUTANDO, TERMINADO, ABORTADO };

// --- ESTRUCTURA DEL GRAFO ---
struct Actividad {
    string id;
    string nombre;
    int tiempo_ms;
    vector<string> dependencias; 
    
    Estado estado = PENDIENTE;
    int dependencias_pendientes = 0; 
    pid_t pid = -1; 
    int pipe_fd[2]; 
};

// --- AISLAMIENTO DE ERRORES (Recursivo) ---
// Función para abortar en cadena si una tarea principal falla
void abortar_rama(string id_fallida, vector<Actividad>& lista) {
    for (auto& act : lista) {
        if (act.estado == PENDIENTE) {
            for (const string& dep : act.dependencias) {
                if (dep == id_fallida) {
                    act.estado = ABORTADO;
                    cout << "[AISLAMIENTO] Abortando: " << act.nombre 
                         << " (Dependía de actividad fallida: " << id_fallida << ")" << endl;
                    
                    // Llamada recursiva para abortar a los que dependían de esta rama caída
                    abortar_rama(act.id, lista);
                    break;
                }
            }
        }
    }
}

// --- FUNCIÓN PRINCIPAL ---
int main(int argc, char* argv[]) {
    // 1. Validar argumentos
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

    // 2. Leer archivo y armar el grafo (Parseo)
    ifstream archivo(archivo_nombre);
    if (!archivo.is_open()) {
        cerr << "Error al abrir el archivo: " << archivo_nombre << endl;
        return 1;
    }

    vector<Actividad> lista_actividades;
    string linea;
    
    // Semilla principal para generar los tiempos aleatorios que falten en el parseo
    srand(time(NULL)); 

    // Función auxiliar rápida para limpiar espacios en blanco al inicio y final de los strings
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

    while (getline(archivo, linea)) {
        if (linea.empty()) continue;
        
        stringstream ss(linea);
        string id, nombre, tiempo_str, deps_str;
        
        // El enunciado exige separación por ':' para los primeros campos
        getline(ss, id, ':');
        getline(ss, nombre, ':');
        getline(ss, tiempo_str, ':');
        getline(ss, deps_str); // El resto son las dependencias
        
        limpiar_espacios(id);
        limpiar_espacios(nombre);
        limpiar_espacios(tiempo_str);

        Actividad nueva_act;
        nueva_act.id = id;
        nueva_act.nombre = nombre;

        // Regla: si no hay tiempo, generar aleatorio entre 100 y 5000 ms
        if (tiempo_str.empty()) {
            nueva_act.tiempo_ms = 100 + rand() % 4901; 
        } else {
            nueva_act.tiempo_ms = stoi(tiempo_str);
        }

        // Las dependencias vienen separadas por comas en el último campo
        stringstream ss_deps(deps_str);
        string dep;
        while (getline(ss_deps, dep, ',')) {
            limpiar_espacios(dep);
            if (!dep.empty() && dep != "Ninguna") {
                nueva_act.dependencias.push_back(dep);
            }
        }
        
        nueva_act.dependencias_pendientes = nueva_act.dependencias.size();
        lista_actividades.push_back(nueva_act);
    }
    archivo.close();

    // 3. Configurar señales e iniciar simulación
    signal(SIGINT, manejar_seremi);
    int procesos_activos = 0;
    int total_tareas = lista_actividades.size();

    cout << "\n--- INICIANDO PLANIFICADOR DIECIOCHERO (K=" << limite_K << ") ---" << endl;

    // 4. Motor Principal (Termina cuando todas las tareas están TERMINADAS o ABORTADAS)
    while (true) {
        int tareas_completadas = 0;
        for (const auto& act : lista_actividades) {
            if (act.estado == TERMINADO || act.estado == ABORTADO) {
                tareas_completadas++;
            }
        }
        // Condición de salida del bucle general
        if (tareas_completadas == total_tareas) break;

        // Lanzar nuevas tareas respetando K
        for (auto& act : lista_actividades) {
            if (procesos_activos >= limite_K) break; 

            if (act.estado == PENDIENTE && act.dependencias_pendientes == 0) {
                
                if (pipe(act.pipe_fd) == -1) {
                    cerr << "Error al crear el pipe." << endl;
                    return 1;
                }

                pid_t pid = fork(); 

                if (pid == 0) {
                    // --- CODIGO DEL HIJO ---
                    // Generar una semilla verdaderamente única para este proceso usando su PID
                    srand(time(NULL) ^ getpid());
                    
                    close(act.pipe_fd[0]); // Cierra extremo de lectura
                    cout << "[HIJO] Ejecutando: " << act.nombre << " (PID: " << getpid() << ") - " << act.tiempo_ms << "ms" << endl;
                    
                    usleep(act.tiempo_ms * 1000); 
                    
                    // Simular fallo aleatorio de la actividad (15% de probabilidad)
                    int suerte = rand() % 100;
                    if (suerte < 15) {
                        cout << "[ERROR] Fallo crítico interno en: " << act.nombre << " (PID: " << getpid() << ")" << endl;
                        close(act.pipe_fd[1]);
                        exit(1); // Retorna código de error al padre
                    }

                    // Propagar mensaje por Pipe
                    string mensaje = "¡Insumo de " + act.nombre + " listo!";
                    write(act.pipe_fd[1], mensaje.c_str(), mensaje.length() + 1);
                    close(act.pipe_fd[1]);
                    
                    exit(0); // Retorna éxito
                } 
                else if (pid > 0) {
                    // --- CODIGO DEL PADRE ---
                    act.pid = pid;          
                    act.estado = EJECUTANDO; 
                    procesos_activos++;     
                    close(act.pipe_fd[1]); // Cierra extremo de escritura
                    pids_activos_global.push_back(pid); // Lo anota para que la Seremi lo vigile
                }
            }
        }

        // Atender a los hijos que terminan
        if (procesos_activos > 0) {
            int status;
            pid_t pid_terminado = wait(&status); 

            if (pid_terminado > 0) {
                procesos_activos--;
                
                // Lo borra de la lista de vigilancia de la Seremi
                pids_activos_global.erase(remove(pids_activos_global.begin(), pids_activos_global.end(), pid_terminado), pids_activos_global.end());

                for (auto& act : lista_actividades) {
                    if (act.pid == pid_terminado) {
                        
                        // Si el hijo terminó correctamente (exit 0)
                        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                            act.estado = TERMINADO;
                            
                            char buffer[256];
                            memset(buffer, 0, sizeof(buffer)); // Limpia la memoria sucia del buffer
                            read(act.pipe_fd[0], buffer, sizeof(buffer));
                            close(act.pipe_fd[0]); 

                            cout << "[MENSAJE PIPE] " << buffer << endl;

                            // Notificar a las tareas que dependían de este insumo
                            for (auto& dep_act : lista_actividades) {
                                if (dep_act.estado == PENDIENTE) {
                                    for (const string& dep : dep_act.dependencias) {
                                        if (dep == act.id) {
                                            dep_act.dependencias_pendientes--;
                                            break;
                                        }
                                    }
                                }
                            }
                        } 
                        // Si el hijo falló (exit distinto de 0)
                        else {
                            act.estado = ABORTADO;
                            close(act.pipe_fd[0]);
                            cout << "[PADRE] Detectado fallo en proceso " << act.nombre << ". Aislando errores..." << endl;
                            
                            // Abortar todo lo que dependía de esta rama
                            abortar_rama(act.id, lista_actividades);
                        }
                        break; // Ya encontramos el proceso, salimos del for
                    }
                }
            }
        }
    }
    
    cout << "--- SIMULACIÓN FINALIZADA ---" << endl;
    return 0;
}