package main

import (
	"context"
	"fmt"
	"log"
	"net"

	"github.com/streadway/amqp"
	"google.golang.org/grpc"

	pb "go-rabbitmq-writer/proto"
)

type Server struct {
	pb.UnimplementedWeatherServiceServer
	rabbitChan *amqp.Channel
}

func (s *Server) PublishWeather(ctx context.Context, req *pb.WeatherRequest) (*pb.WeatherResponse, error) {
	// Format the message
	body := fmt.Sprintf("Description: %s, Country: %s, Weather: %s", req.Description, req.Country, req.Weather)

	// Publish to RabbitMQ
	err := s.rabbitChan.Publish(
		"",        // exchange
		"message", // queue name
		false,     // mandatory
		false,     // immediate
		amqp.Publishing{
			ContentType: "text/plain",
			Body:        []byte(body),
		},
	)
	if err != nil {
		log.Printf("Failed to publish to RabbitMQ: %v", err)
		return nil, err
	}

	// Log the message for confirmation
	log.Printf("Published to RabbitMQ: %s", body)

	return &pb.WeatherResponse{Message: "Published to RabbitMQ"}, nil
}

func main() {
	// Connect to RabbitMQ
	conn, err := amqp.Dial("amqp://guest:guest@rabbitmq-service.proyecto2.svc.cluster.local:5672/")
	if err != nil {
		log.Fatalf("Failed to connect to RabbitMQ: %v", err)
	}
	defer conn.Close()

	ch, err := conn.Channel()
	if err != nil {
		log.Fatalf("Failed to open a channel: %v", err)
	}
	defer ch.Close()

	// Declare the queue
	_, err = ch.QueueDeclare(
		"message", // name
		false,     // durable
		false,     // delete when unused
		false,     // exclusive
		false,     // no-wait
		nil,       // arguments
	)
	if err != nil {
		log.Fatalf("Failed to declare a queue: %v", err)
	}

	// Start gRPC server
	lis, err := net.Listen("tcp", ":50051")
	if err != nil {
		log.Fatalf("Failed to listen: %v", err)
	}

	s := grpc.NewServer()
	pb.RegisterWeatherServiceServer(s, &Server{rabbitChan: ch})

	log.Printf("gRPC server listening on :50051")
	if err := s.Serve(lis); err != nil {
		log.Fatalf("Failed to serve: %v", err)
	}
}
