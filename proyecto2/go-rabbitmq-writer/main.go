package main

import (
	"context"
	"log"
	"net"

	"google.golang.org/grpc"

	pb "go-rabbitmq-writer/proto"
)

type Server struct {
	pb.UnimplementedWeatherServiceServer
}

func (s *Server) PublishWeather(ctx context.Context, req *pb.WeatherRequest) (*pb.WeatherResponse, error) {
	// Print the received data
	log.Printf("Received Weather Data: Description: %s, Country: %s, Weather: %s",
		req.Description, req.Country, req.Weather)

	return &pb.WeatherResponse{Message: "Data received and logged"}, nil
}

func main() {
	// Start gRPC server
	lis, err := net.Listen("tcp", ":50051")
	if err != nil {
		log.Fatalf("Failed to listen: %v", err)
	}

	s := grpc.NewServer()
	pb.RegisterWeatherServiceServer(s, &Server{})

	log.Printf("gRPC server listening on :50051")
	if err := s.Serve(lis); err != nil {
		log.Fatalf("Failed to serve: %v", err)
	}
}
