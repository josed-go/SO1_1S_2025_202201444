use actix_web::{web, App, HttpResponse, HttpServer, Responder};
use serde::{Deserialize, Serialize};
use reqwest::Client;

#[derive(Deserialize, Serialize, Debug)]
struct WeatherData {
    description: String,
    country: String,
    weather: String,
}

async fn handle_weather_data(data: web::Json<WeatherData>, client: web::Data<Client>) -> impl Responder {
    // Log the received data
    println!("Received: {:?}", data);

    // Send the data to the Go API (replace with your Go service URL)
    let go_api_url = "http://go-api-service.proyecto2.svc.cluster.local:80/weather";
    let response = client
        .post(go_api_url)
        .json(&data)
        .send()
        .await;

    match response {
        Ok(res) if res.status().is_success() => {
            HttpResponse::Ok().json("Data sent to Go API successfully")
        }
        Ok(res) => {
            eprintln!("Error from Go API: {:?}", res.status());
            HttpResponse::BadGateway().json("Failed to send data to Go API")
        }
        Err(e) => {
            eprintln!("Error sending to Go API: {:?}", e);
            HttpResponse::BadGateway().json("Error communicating with Go API")
        }
    }
}

#[actix_web::main]
async fn main() -> std::io::Result<()> {
    println!("Starting Rust API on port 8080...");
    let client = Client::new();
    HttpServer::new(move || {
        App::new()
            .app_data(web::Data::new(client.clone()))
            .route("/input", web::post().to(handle_weather_data))
    })
    .bind(("0.0.0.0", 8080))?
    .run()
    .await
}