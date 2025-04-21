package main

import (
	"fmt"
	"log"

	"github.com/gofiber/fiber/v2"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"

	pb "go-api/proto"
)

type WeatherData struct {
	Description string `json:"description"`
	Country     string `json:"country"`
	Weather     string `json:"weather"`
}

func main() {
	// Connect to gRPC server (go-kafka-writer)
	kafkaConn, err := grpc.Dial("go-kafka-writer-service.proyecto2.svc.cluster.local:50051", grpc.WithTransportCredentials(insecure.NewCredentials()))
	if err != nil {
		log.Fatalf("Failed to connect to Kafka gRPC server: %v", err)
	}
	defer kafkaConn.Close()
	kafkaClient := pb.NewWeatherServiceClient(kafkaConn)

	// Connect to gRPC server (go-rabbitmq-writer)
	rabbitConn, err := grpc.Dial("go-rabbitmq-writer-service.proyecto2.svc.cluster.local:50051", grpc.WithTransportCredentials(insecure.NewCredentials()))
	if err != nil {
		log.Fatalf("Failed to connect to RabbitMQ gRPC server: %v", err)
	}
	defer rabbitConn.Close()
	rabbitClient := pb.NewWeatherServiceClient(rabbitConn)

	// Start Fiber server for REST API
	app := fiber.New()

	app.Post("/weather", func(c *fiber.Ctx) error {
		var data WeatherData
		if err := c.BodyParser(&data); err != nil {
			return c.Status(fiber.StatusBadRequest).JSON(fiber.Map{"error": "Cannot parse JSON"})
		}

		// Prepare the gRPC request
		req := &pb.WeatherRequest{
			Description: data.Description,
			Country:     data.Country,
			Weather:     data.Weather,
		}

		// Send to go-kafka-writer
		kafkaRes, err := kafkaClient.PublishWeather(c.Context(), req)
		if err != nil {
			log.Printf("Failed to send to Kafka gRPC server: %v", err)
			return c.Status(fiber.StatusInternalServerError).JSON(fiber.Map{"error": "Failed to send to Kafka gRPC server"})
		}

		// Send to go-rabbitmq-writer
		rabbitRes, err := rabbitClient.PublishWeather(c.Context(), req)
		if err != nil {
			log.Printf("Failed to send to RabbitMQ gRPC server: %v", err)
			return c.Status(fiber.StatusInternalServerError).JSON(fiber.Map{"error": "Failed to send to RabbitMQ gRPC server"})
		}

		return c.JSON(fiber.Map{"message": fmt.Sprintf("Kafka: %s, RabbitMQ: %s", kafkaRes.Message, rabbitRes.Message)})
	})

	log.Fatal(app.Listen(":80"))
}
