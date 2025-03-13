from fastapi import FastAPI, HTTPException
from pydantic import BaseModel
from typing import Optional, List
import json
import os
import matplotlib.pyplot as plt

app = FastAPI()

LOG_FILE = "/container/logs/logs.json"

class LogEntry(BaseModel):
    container_name: str
    container_id: str
    created_at: str
    deleted_at: Optional[str] = None
    cpu_usage_percent: float = 0.0
    memory_usage_percent: float = 0.0
    disk_usage_mb: int = 0
    block_io_read_mb: int = 0
    block_io_write_mb: int = 0

class CategorizedLogs(BaseModel):
    CPU: List[LogEntry] = []
    RAM: List[LogEntry] = []
    IO: List[LogEntry] = []
    Disk: List[LogEntry] = []

# Inicializar el archivo si no existe o está vacío/corrupto
if not os.path.exists(LOG_FILE) or os.path.getsize(LOG_FILE) == 0:
    with open(LOG_FILE, "w") as f:
        json.dump({"CPU": [], "RAM": [], "IO": [], "Disk": []}, f, indent=2)

@app.post("/logs")
async def receive_logs(logs: CategorizedLogs):
    try:
        # Leer los logs existentes
        with open(LOG_FILE, "r") as f:
            existing_logs = json.load(f)
        
        # Combinar los nuevos logs con los existentes
        new_logs = logs.dict()
        for category in new_logs:
            existing_logs[category].extend(new_logs[category])
        
        # Guardar los logs actualizados
        with open(LOG_FILE, "w") as f:
            json.dump(existing_logs, f, indent=2)
        
        return {"message": "Logs recibidos y guardados"}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/generate_graphs")
async def generate_graphs():
    try:
        with open(LOG_FILE, "r") as f:
            logs = json.load(f)
        if not any(logs.values()):  # Verificar si todas las categorías están vacías
            return {"message": "No hay logs para generar gráficas"}

        # Contar contenedores por categoría
        cpu_usage = {cat: len(log_list) for cat, log_list in logs.items()}
        plt.figure(figsize=(10, 6))
        plt.bar(cpu_usage.keys(), cpu_usage.values(), color=['blue', 'orange', 'green', 'red'])
        plt.title("Uso por Categoría (Número de Contenedores)")
        plt.xlabel("Categoría")
        plt.ylabel("Número de Contenedores")
        plt.savefig("/container/logs/cpu_usage.png")
        plt.close()

        # Contar contenedores eliminados por categoría
        deleted_counts = {
            cat: sum(1 for log in log_list if log["deleted_at"] is not None)
            for cat, log_list in logs.items()
        }
        plt.figure(figsize=(10, 6))
        plt.pie(deleted_counts.values(), labels=deleted_counts.keys(), autopct='%1.1f%%', colors=['blue', 'orange', 'green', 'red'])
        plt.title("Distribución de Contenedores Eliminados")
        plt.savefig("/container/logs/deleted_containers.png")
        plt.close()

        return {"message": "Gráficas generadas en /container/logs"}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/logs")
async def get_logs():
    try:
        with open(LOG_FILE, "r") as f:
            logs = json.load(f)
        return logs
    except json.JSONDecodeError:
        # Si el archivo está corrupto, devolver un JSON vacío y sobrescribirlo
        default_logs = {"CPU": [], "RAM": [], "IO": [], "Disk": []}
        with open(LOG_FILE, "w") as f:
            json.dump(default_logs, f, indent=2)
        return default_logs
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))