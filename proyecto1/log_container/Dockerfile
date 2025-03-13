# Imagen base
FROM python:3.9-slim

# Directorio de trabajo
WORKDIR /app

# Copiar archivos
COPY requerimientos.txt .
COPY main.py .

# Instalar dependencias
RUN pip install --no-cache-dir -r requerimientos.txt

# Crear directorio para logs
RUN mkdir -p /container/logs

# Exponer puerto
EXPOSE 8080

# Comando para iniciar el servidor
CMD ["uvicorn", "main:app", "--host", "0.0.0.0", "--port", "8080"]