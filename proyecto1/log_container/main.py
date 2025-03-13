from fastapi import FastAPI, HTTPException
from pydantic import BaseModel
from typing import Optional  # Importamos Optional
import json
import os
import matplotlib.pyplot as plt

app = FastAPI()

LOG_FILE = "/container/logs/logs.json"

class LogEntry(BaseModel):
    container_name: str
    container_id: str
    category: str
    created_at: str
    deleted_at: Optional[str] = None  # Campo opcional, como en Rust
    cpu_usage_percent: float = 0.0    # Campo para el uso de CPU
    memory_usage_percent: float = 0.0 # Campo para el uso de memoria
    disk_usage_mb: int = 0            # Campo para el uso de disco (u64 en Rust, int en Python)
    block_io_read_mb: int = 0         # Campo para lectura de block_io
    block_io_write_mb: int = 0        # Campo para escritura de block_io

if not os.path.exists(LOG_FILE):
    with open(LOG_FILE, "w") as f:
        json.dump([], f)

@app.post("/logs")
async def receive_logs(logs: list[LogEntry]):
    try:
        with open(LOG_FILE, "r") as f:
            existing_logs = json.load(f)
        new_logs = [log.dict() for log in logs]
        existing_logs.extend(new_logs)
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
        if not logs:
            return {"message": "No hay logs para generar gráficas"}
        
        categories = {"CPU": [], "RAM": [], "I/O": [], "Disk": []}
        for log in logs:
            category = log["category"]
            if category in categories:
                categories[category].append(log)
        
        cpu_usage = {cat: len(logs) for cat, logs in categories.items()}  # Ejemplo simple
        plt.figure(figsize=(10, 6))
        plt.bar(cpu_usage.keys(), cpu_usage.values(), color=['blue', 'orange', 'green', 'red'])
        plt.title("Uso por Categoría (Ejemplo)")
        plt.xlabel("Categoría")
        plt.ylabel("Número de Contenedores")
        plt.savefig("/container/logs/cpu_usage.png")
        plt.close()

        deleted_counts = {cat: sum(1 for log in logs if log["deleted_at"] is not None) for cat, logs in categories.items()}
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
    with open(LOG_FILE, "r") as f:
        logs = json.load(f)
    return logs