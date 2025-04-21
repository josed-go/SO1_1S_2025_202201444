package main

import (
	"context"
	"fmt"
	"log"
	"net"

	"github.com/streadway/amqp"
	"google.golang.org/grpc"

	pb "go-api/proto"
)

type Server struct {
	pb.UnimplementedWeatherServiceServer
	kafkaChan  chan *pb.WeatherRequest
	rabbitChan *amqp.Channel
}

func (s *Server) PublishWeather(ctx context.Context, req *pb.WeatherRequest) (*pb.WeatherResponse, error) {
	// Send to Kafka channel
	s.kafkaChan <- req

	// Send to RabbitMQ
	body := fmt.Sprintf("Description: %s, Country: %s, Weather: %s", req.Description, req.Country, req.Weather)
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
	}

	return &pb.WeatherResponse{Message: "Published to Kafka and RabbitMQ"}, nil
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

	// Kafka channel (simulated here, actual Kafka producer logic will be in Go Deployment 2)
	kafkaChan := make(chan *pb.WeatherRequest)

	// Start gRPC server
	lis, err := net.Listen("tcp", ":50051")
	if err != nil {
		log.Fatalf("Failed to listen: %v", err)
	}

	s := grpc.NewServer()
	pb.RegisterWeatherServiceServer(s, &Server{
		kafkaChan:  kafkaChan,
		rabbitChan: ch,
	})

	log.Printf("gRPC server listening on :50051")
	if err := s.Serve(lis); err != nil {
		log.Fatalf("Failed to serve: %v", err)
	}
}
