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
	// Connect to gRPC server (this will be the internal gRPC client in the same pod)
	conn, err := grpc.Dial("localhost:50051", grpc.WithTransportCredentials(insecure.NewCredentials()))
	if err != nil {
		log.Fatalf("Failed to connect to gRPC server: %v", err)
	}
	defer conn.Close()

	client := pb.NewWeatherServiceClient(conn)

	// Start Fiber server for REST API
	app := fiber.New()

	app.Post("/weather", func(c *fiber.Ctx) error {
		var data WeatherData
		if err := c.BodyParser(&data); err != nil {
			return c.Status(fiber.StatusBadRequest).JSON(fiber.Map{"error": "Cannot parse JSON"})
		}

		// Send data to gRPC server
		req := &pb.WeatherRequest{
			Description: data.Description,
			Country:     data.Country,
			Weather:     data.Weather,
		}
		res, err := client.PublishWeather(c.Context(), req)
		if err != nil {
			return c.Status(fiber.StatusInternalServerError).JSON(fiber.Map{"error": fmt.Sprintf("gRPC error: %v", err)})
		}

		return c.JSON(fiber.Map{"message": res.Message})
	})

	log.Fatal(app.Listen(":80"))
}
