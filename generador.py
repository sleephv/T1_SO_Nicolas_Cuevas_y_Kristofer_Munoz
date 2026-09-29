import random

NUM_TAREAS = 200
nombres = ["prender_carbon", "comprar_carne", "cortar_cebolla", "armar_choripan", 
           "servir_mesa", "hacer_pebre", "comprar_empanadas", "preparar_terremoto", 
           "bailar_cueca", "saludar_vecino"]

with open("plan_estres.txt", "w") as f:
    for i in range(1, NUM_TAREAS + 1):
        nombre = random.choice(nombres)
        
        # El 30% de las tareas no tendrá tiempo para probar el rand() del parseo
        if random.random() < 0.3:
            tiempo = ""
        else:
            tiempo = str(random.randint(100, 2000))
            
        # Dependencias lógicas: cada tarea (a partir de la 2) dependerá de 1 a 3 tareas anteriores
        dependencias = []
        if i > 1:
            num_deps = random.randint(1, min(3, i - 1))
            deps_elegidas = random.sample(range(1, i), num_deps)
            dependencias = [str(d) for d in sorted(deps_elegidas)]
            
        deps_str = ", ".join(dependencias)
        
        # Escribir la línea con el formato estricto: ID : Nombre : tiempo : [dependencias]
        f.write(f"{i} : {nombre} : {tiempo} : {deps_str}\n")

print(f"¡Archivo plan_estres.txt generado con {NUM_TAREAS} actividades!")