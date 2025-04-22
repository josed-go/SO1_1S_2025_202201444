from locust import HttpUser, task, between
import random

# Diccionario que asocia tipos de clima con descripciones relevantes
weather_descriptions = {
    "Lluvioso": [
        "Está lloviendo",
        "Tormenta eléctrica",
        "Llovizna ligera",
        "Lluvia intensa",
        "Chubascos fuertes",
        "Cielo tormentoso",
        "Lluvia persistente"
    ],
    "Nublado": [
        "Día nublado",
        "Cielo cubierto",
        "Nubes grises",
        "Niebla ligera",
        "Cielo encapotado",
        "Día gris",
        "Nubosidad densa"
    ],
    "Soleado": [
        "Cielo despejado",
        "Día soleado",
        "Sol brillante",
        "Cielo azul",
        "Día cálido",
        "Sin nubes",
        "Sol radiante"
    ]
}

countries = ["GT", "USA", "MX", "BR", "CO"]

# Generar un arreglo de 10,000 objetos JSON con datos consistentes
test_data = []
weather_types = list(weather_descriptions.keys())  # ["Lluvioso", "Nublado", "Soleado"]

for _ in range(10000):
    # Seleccionar un tipo de clima
    weather = random.choice(weather_types)
    # Seleccionar una descripción que coincida con el tipo de clima
    description = random.choice(weather_descriptions[weather])
    # Seleccionar un país
    country = random.choice(countries)
    # Crear el objeto JSON
    test_data.append({
        "description": description,
        "country": country,
        "weather": weather
    })

class WeatherUser(HttpUser):
    wait_time = between(1, 5)  # Tiempo de espera entre solicitudes (1 a 5 segundos)

    @task
    def send_weather_data(self):
        # Seleccionar un objeto aleatorio del arreglo de datos
        data = random.choice(test_data)
        self.client.post(
            "/input",
            json=data,
            headers={"Content-Type": "application/json"}
        )