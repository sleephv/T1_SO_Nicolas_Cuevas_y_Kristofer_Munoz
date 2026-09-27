#include <iostream>  
#include <fstream>   // Para leer el archivo plan.txt
#include <sstream>   // Para separar los textos de cada línea
#include <vector>    // Para guardar nuestras actividades dinámicamente
#include <string>    // Para manejar textos
#include <cstdlib>   // Para funciones como exit() o stoi()
#include <unistd.h>  // Aquí vive la syscall fork()
#include <sys/wait.h>// Aquí vive la syscall waitpid()
#include <random>    // Para generar tiempos aleatorios si faltan
#include <cstring>   

using namespace std;
enum Estado { PENDIENTE, EN_EJECUCION, TERMINADO };

// Estructura que representa un nodo de nuestro Grafo (DAG)
struct Actividad {
    string id;
    string nombre;
    int tiempo_ms;
    vector<string> dependencias; // Guarda los IDs de las tareas que deben terminar antes
    std::size_t dependencias_pendientes; // Contador de dependencias que aún no se han completado
    Estado estado = PENDIENTE; 
    int depencias_pendientes = 0; // Contador de dependencias que aún no se han completado
    pid_t pid = -1; // PID del proceso hijo que ejecuta esta actividad
    int pipe_fd[2]; // Descriptor de archivos para el pipe
};

int main(int argc, char* argv[]) {
    // 1. Validar que el usuario nos pase exactamente 3 argumentos
    // (Ejemplo: ./planificador plan.txt 3)
    if (argc != 3) {
        cerr << "Error de uso. Forma correcta: " << argv[0] << " <archivo.txt> <K>" << endl;
        return 1; // Salimos con error
    }

    // 2. Guardar los argumentos en variables amigables
    string nombre_archivo = argv[1];
    int limite_K = stoi(argv[2]); // stoi convierte de texto ("3") a número (3)

    // 3. Validar que K tenga sentido
    if (limite_K <= 0) {
        cerr << "Error: El límite de concurrencia K debe ser mayor a 0." << endl;
        return 1;
    }

    // Un mensajito para ver que vamos bien
    cout << "Iniciando simulador con K=" << limite_K << " y archivo: " << nombre_archivo << endl;

    // 4. Abrir el archivo plan.txt
    ifstream archivo(nombre_archivo);
    if (!archivo.is_open()) {
        cerr << "Error: No se pudo abrir el archivo " << nombre_archivo << endl;
        return 1;
    }

    vector<Actividad> lista_actividades;
    string linea;

    // 5. Leer línea por línea
    while (getline(archivo, linea)) {
        if (linea.empty()) continue; // Ignorar líneas en blanco

        stringstream ss(linea);
        string id, nombre, tiempo_str, deps_str;

        // Extraer los campos separados por ':'
        getline(ss, id, ':');
        getline(ss, nombre, ':');
        getline(ss, tiempo_str, ':');
        getline(ss, deps_str); // El resto son las dependencias

        Actividad nueva_act;
        
        // Limpiamos posibles espacios extra en el ID y Nombre
        id.erase(0, id.find_first_not_of(" \t\r\n"));
        id.erase(id.find_last_not_of(" \t\r\n") + 1);
        nombre.erase(0, nombre.find_first_not_of(" \t\r\n"));
        nombre.erase(nombre.find_last_not_of(" \t\r\n") + 1);
        
        nueva_act.id = id;
        nueva_act.nombre = nombre;

        // Limpiar espacios en blanco del tiempo
        if (!tiempo_str.empty() && tiempo_str.find_first_not_of(" \t\r\n") != string::npos) {
            tiempo_str.erase(0, tiempo_str.find_first_not_of(" \t\r\n"));
            tiempo_str.erase(tiempo_str.find_last_not_of(" \t\r\n") + 1);
        } else {
            tiempo_str = ""; // Asegurarnos de que quede vacío si solo eran espacios
        }

        // Si no hay tiempo, generar uno aleatorio entre 100 y 5000 ms (Regla de la rúbrica)
        if (tiempo_str.empty()) {
            random_device rd;
            mt19937 gen(rd());
            uniform_int_distribution<> dis(100, 5000);
            nueva_act.tiempo_ms = dis(gen);
        } else {
            nueva_act.tiempo_ms = stoi(tiempo_str);
        }

        // Procesar dependencias (deps_str) separándolas por comas
        if (!deps_str.empty()) {
            stringstream ss_deps(deps_str);
            string dep;
            while (getline(ss_deps, dep, ',')) {
                // Limpiar espacios en blanco al inicio y al final de cada dependencia
                if (dep.find_first_not_of(" \t\r\n") != string::npos) {
                    dep.erase(0, dep.find_first_not_of(" \t\r\n"));
                    dep.erase(dep.find_last_not_of(" \t\r\n") + 1);
                    if (!dep.empty()) {
                        nueva_act.dependencias.push_back(dep);
                    }
                }
            }
        }
        
        lista_actividades.push_back(nueva_act);
        
        // Mostrar en pantalla para confirmar que leyó todo bien
        cout << "Actividad cargada -> ID: " << nueva_act.id 
             << " | Nombre: " << nueva_act.nombre 
             << " | Tiempo: " << nueva_act.tiempo_ms << "ms"
             << " | Dependencias: ";
        
        if (nueva_act.dependencias.empty()) {
            cout << "Ninguna";
        } else {
            for (const string& d : nueva_act.dependencias) {
                cout << d << " ";
            }
        }
        cout << endl;
    }
    int procesos_activos = 0;
    int tareas_terminadas = 0;
    int total_tareas = lista_actividades.size();

    // 1. Contar cuántas dependencias tiene que esperar cada tarea al inicio
    for (auto& act : lista_actividades) {
        act.dependencias_pendientes = act.dependencias.size();
    }

    cout << "\n--- INICIANDO SIMULACIÓN DIE CIOCHERA ---" << endl;

   // Bucle principal: se repite hasta que todas las tareas estén TERMINADAS
    while (tareas_terminadas < total_tareas) {
        
        // Lanzar nuevas tareas si tenemos espacio
        for (auto& act : lista_actividades) {
            if (procesos_activos >= limite_K) break; 

            if (act.estado == PENDIENTE && act.dependencias_pendientes == 0) {
                
                // NUEVO: Crear la tubería (pipe) ANTES del fork
                if (pipe(act.pipe_fd) == -1) {
                    cerr << "Error al crear el pipe para " << act.nombre << endl;
                    return 1;
                }

                pid_t pid = fork(); 

                if (pid == 0) {
                    // ---- CÓDIGO DEL PROCESO HIJO ----
                    close(act.pipe_fd[0]); // El hijo no va a leer, cerramos ese extremo

                    cout << "[HIJO] Iniciando: " << act.nombre << " (PID: " << getpid() << ")" << endl;
                    usleep(act.tiempo_ms * 1000); // Simulamos el trabajo
                    
                    // NUEVO: El hijo escribe el mensaje en el pipe antes de terminar
                    string mensaje = "¡Insumo de " + act.nombre + " listo!";
                    write(act.pipe_fd[1], mensaje.c_str(), mensaje.length() + 1);
                    close(act.pipe_fd[1]); // Cerramos escritura
                    
                    exit(0); 
                } 
                else if (pid > 0) {
                    // ---- CÓDIGO DEL PROCESO PADRE ----
                    act.pid = pid;          
                    act.estado = EN_EJECUCION; 
                    procesos_activos++;     
                    close(act.pipe_fd[1]); // NUEVO: El padre no va a escribir, cierra ese extremo
                }
            }
        }

        // Esperar a que algún proceso hijo termine
        if (procesos_activos > 0) {
            int status;
            pid_t pid_terminado = wait(&status); 

            if (pid_terminado > 0) {
                procesos_activos--;
                tareas_terminadas++;

                // Buscar qué actividad terminó
                for (auto& act : lista_actividades) {
                    if (act.pid == pid_terminado) {
                        act.estado = TERMINADO;
                        
                        // NUEVO: El padre lee el mensaje que dejó el hijo en el pipe
                        char buffer[256];
                        read(act.pipe_fd[0], buffer, sizeof(buffer));
                        close(act.pipe_fd[0]); // Cerramos lectura

                        cout << "[MENSAJE PIPE] " << buffer << endl;

                        // Avisar a las demás tareas y propagar el mensaje
                        for (auto& dep_act : lista_actividades) {
                            if (dep_act.estado == PENDIENTE) {
                                for (const string& dep : dep_act.dependencias) {
                                    if (dep == act.id) {
                                        dep_act.dependencias_pendientes--;
                                        cout << " -> Propagando a: " << dep_act.nombre 
                                             << " (Faltan " << dep_act.dependencias_pendientes << " dependencias)" << endl;
                                        break;
                                    }
                                }
                            }
                        }
                        break;
                    }
                }
            }
        }
    }
    cout << "--- SIMULACIÓN FINALIZADA CORRECTAMENTE ---" << endl;
    // =================================================================

    archivo.close();

    return 0; // Termina el programa con éxito
}