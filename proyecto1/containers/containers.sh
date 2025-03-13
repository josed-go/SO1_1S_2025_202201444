#!/bin/bash

# Función para generar un nombre aleatorio para los contenedores
generate_name() {
    echo "container_$(date +%s)_$RANDOM"
}

# Lista de imágenes a utilizar
IMAGES=(
    "containerstack/alpine-stress:latest"
)

# Cantidad de contenedores a generar
NUM_CONTAINERS=10

for ((i=0; i<$NUM_CONTAINERS; i++)); do
    # Seleccionar un tipo de contenedor aleatorio
    TYPE=$((RANDOM % 4))
    
    # Generar un nombre único
    CONTAINER_NAME=$(generate_name)

    case $TYPE in
        0)
            echo "Creando contenedor de RAM (moderado): $CONTAINER_NAME"
            docker run -d --name "$CONTAINER_NAME" --memory="128m" "${IMAGES[0]}" stress --vm 1 --vm-bytes 64M
            ;;
        1)
            echo "Creando contenedor de CPU (moderado): $CONTAINER_NAME"
            docker run -d --name "$CONTAINER_NAME" --cpus="0.5" "${IMAGES[0]}" stress --cpu 1
            ;;
        2)
            echo "Creando contenedor de I/O (moderado): $CONTAINER_NAME"
            docker run -d --name "$CONTAINER_NAME" "${IMAGES[0]}" stress --io 2
            ;;
        3)
            echo "Creando contenedor de Disco (moderado): $CONTAINER_NAME"
            docker run -d --name "$CONTAINER_NAME" "${IMAGES[0]}" stress --hdd 1 --hdd-bytes 64M
            ;;
    esac
done
