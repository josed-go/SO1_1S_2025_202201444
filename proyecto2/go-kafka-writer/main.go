package main

import (
	"context"
	"fmt"
	"log"
	"net"

	"github.com/segmentio/kafka-go"
	"google.golang.org/grpc"

	pb "go-kafka-writer/proto"
)

type Server struct {
	pb.UnimplementedWeatherServiceServer
	writer *kafka.Writer
}

func (s *Server) PublishWeather(ctx context.Context, req *pb.WeatherRequest) (*pb.WeatherResponse, error) {
	// Format the message
	body := fmt.Sprintf("Description: %s, Country: %s, Weather: %s", req.Description, req.Country, req.Weather)

	// Publish to Kafka
	err := s.writer.WriteMessages(ctx, kafka.Message{
		Value: []byte(body),
	})
	if err != nil {
		log.Printf("Failed to write to Kafka: %v", err)
		return nil, err
	}

	// Log the message for confirmation
	log.Printf("Published to Kafka: %s", body)

	return &pb.WeatherResponse{Message: "Published to Kafka"}, nil
}

func main() {
	// Connect to Kafka
	writer := &kafka.Writer{
		Addr:                   kafka.TCP("my-kafka-kafka-bootstrap.proyecto2.svc.cluster.local:9092"),
		Topic:                  "weather",
		Balancer:               &kafka.LeastBytes{},
		AllowAutoTopicCreation: true,
	}
	defer writer.Close()

	// Start gRPC server
	lis, err := net.Listen("tcp", ":50051")
	if err != nil {
		log.Fatalf("Failed to listen: %v", err)
	}

	s := grpc.NewServer()
	pb.RegisterWeatherServiceServer(s, &Server{writer: writer})

	log.Printf("gRPC server listening on :50051")
	if err := s.Serve(lis); err != nil {
		log.Fatalf("Failed to serve: %v", err)
	}
}
