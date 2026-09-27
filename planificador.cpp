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
#include <cstdlib> // Para rand()
#include <ctime>   // Para time()

using namespace std;

vector<pid_t> pids_activos_global;

void manejar_seremi(int sig) {
    (void)sig; // Evita el warning del compilador por parámetro sin usar
    cout << "\n\n[SEREMI] ¡Inspección de salubridad sorpresa (Ctrl+C) detectada!" << endl;
    cout << "[SEREMI] Abortando todos los procesos activos..." << endl;
    
    for (pid_t pid : pids_activos_global) {
        kill(pid, SIGTERM);
    }
    
    cout << "[SEREMI] Clausura completada. Saliendo del planificador limpiamente." << endl;
    exit(1);
}

enum Estado { PENDIENTE, EJECUTANDO, TERMINADO, ABORTADO };

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

// Función para abortar en cadena si una tarea principal falla
void abortar_rama(string id_fallida, vector<Actividad>& lista) {
    for (auto& act : lista) {
        if (act.estado == PENDIENTE) {
            for (const string& dep : act.dependencias) {
                if (dep == id_fallida) {
                    act.estado = ABORTADO;
                    cout << "[AISLAMIENTO] Abortando: " << act.nombre 
                         << " (Dependía de actividad fallida: " << id_fallida << ")" << endl;
                    
                    // Llamada recursiva para abortar a los que dependían de esta
                    abortar_rama(act.id, lista);
                    break;
                }
            }
        }
    }
}


int main(int argc, char* argv[]) {
    // 1. Validar argumentos
    if (argc != 3) {
        cerr << "Uso: ./planificador <archivo.txt> <K_procesos_concurrentes>" << endl;
        return 1;
    }

    string archivo_nombre = argv[1];
    int limite_K = stoi(argv[2]);

    // 2. Leer archivo y armar el grafo
    ifstream archivo(archivo_nombre);
    if (!archivo.is_open()) {
        cerr << "Error al abrir el archivo: " << archivo_nombre << endl;
        return 1;
    }

    vector<Actividad> lista_actividades;
    string linea;
    srand(time(NULL)); // Inicializar semilla para fallos aleatorios

    while (getline(archivo, linea)) {
        if (linea.empty()) continue;
        
        stringstream ss(linea);
        Actividad nueva_act;
        
        getline(ss, nueva_act.id, ',');
        getline(ss, nueva_act.nombre, ',');
        
        string tiempo_str;
        getline(ss, tiempo_str, ',');
        if (tiempo_str.empty()) {
            nueva_act.tiempo_ms = 100 + rand() % 4901; 
        } else {
            nueva_act.tiempo_ms = stoi(tiempo_str);
        }

        string dep;
        while (getline(ss, dep, ',')) {
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

    cout << "\n--- INICIANDO PLANIFICADOR DIE CIOCHERO (K=" << limite_K << ") ---" << endl;

    // Bucle principal (Termina cuando todas las tareas están TERMINADAS o ABORTADAS)
    while (true) {
        int tareas_completadas = 0;
        for (const auto& act : lista_actividades) {
            if (act.estado == TERMINADO || act.estado == ABORTADO) {
                tareas_completadas++;
            }
        }
        if (tareas_completadas == total_tareas) break;

        // Lanzar nuevas tareas
        for (auto& act : lista_actividades) {
            if (procesos_activos >= limite_K) break; 

            if (act.estado == PENDIENTE && act.dependencias_pendientes == 0) {
                
                if (pipe(act.pipe_fd) == -1) {
                    cerr << "Error al crear el pipe." << endl;
                    return 1;
                }

                pid_t pid = fork(); 

                if (pid == 0) {
                    // --- HIJO ---
                    close(act.pipe_fd[0]); 
                    cout << "[HIJO] Ejecutando: " << act.nombre << " (PID: " << getpid() << ") - " << act.tiempo_ms << "ms" << endl;
                    
                    usleep(act.tiempo_ms * 1000); 
                    
                    // Simular un fallo aleatorio (15% de probabilidad)
                    int suerte = rand() % 100;
                    if (suerte < 15) {
                        cout << "[ERROR] Fallo crítico interno en: " << act.nombre << " (PID: " << getpid() << ")" << endl;
                        close(act.pipe_fd[1]);
                        exit(1); // Retorna error
                    }

                    string mensaje = "¡Insumo de " + act.nombre + " listo!";
                    write(act.pipe_fd[1], mensaje.c_str(), mensaje.length() + 1);
                    close(act.pipe_fd[1]);
                    
                    exit(0); // Retorna éxito
                } 
                else if (pid > 0) {
                    // --- PADRE ---
                    act.pid = pid;          
                    act.estado = EJECUTANDO; 
                    procesos_activos++;     
                    close(act.pipe_fd[1]); 
                    pids_activos_global.push_back(pid); // Anotar para la Seremi
                }
            }
        }

        // Atender a los hijos que terminan
        if (procesos_activos > 0) {
            int status;
            pid_t pid_terminado = wait(&status); 

            if (pid_terminado > 0) {
                procesos_activos--;
                pids_activos_global.erase(remove(pids_activos_global.begin(), pids_activos_global.end(), pid_terminado), pids_activos_global.end());

                for (auto& act : lista_actividades) {
                    if (act.pid == pid_terminado) {
                        
                        // Si el hijo terminó correctamente (exit 0)
                        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                            act.estado = TERMINADO;
                            
                            char buffer[256];
                            read(act.pipe_fd[0], buffer, sizeof(buffer));
                            close(act.pipe_fd[0]); 

                            cout << "[MENSAJE PIPE] " << buffer << endl;

                            // Propagar a dependencias
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
                        break;
                    }
                }
            }
        }
    }
    
    cout << "--- SIMULACIÓN FINALIZADA ---" << endl;
    return 0;
}