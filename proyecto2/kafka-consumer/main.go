package main

import (
	"context"
	"log"
	"strings"

	"github.com/redis/go-redis/v9"
	"github.com/segmentio/kafka-go"
)

func main() {
	// Connect to Kafka
	reader := kafka.NewReader(kafka.ReaderConfig{
		Brokers:  []string{"my-kafka-kafka-bootstrap.proyecto2.svc.cluster.local:9092"},
		Topic:    "weather",
		GroupID:  "kafka-consumer-group",
		MinBytes: 10e3, // 10KB
		MaxBytes: 10e6, // 10MB
	})
	defer reader.Close()

	// Connect to Redis
	redisClient := redis.NewClient(&redis.Options{
		Addr: "redis-service.proyecto2.svc.cluster.local:6379",
	})
	defer redisClient.Close()

	// Test Redis connection
	_, err := redisClient.Ping(context.Background()).Result()
	if err != nil {
		log.Fatalf("Failed to connect to Redis: %v", err)
	}

	log.Println("Connected to Kafka and Redis. Starting to consume messages...")

	// Consume messages from Kafka and store in Redis
	for {
		msg, err := reader.ReadMessage(context.Background())
		if err != nil {
			log.Printf("Failed to read message from Kafka: %v", err)
			continue
		}

		// Parse the message (format: "Description: ..., Country: ..., Weather: ...")
		message := string(msg.Value)
		log.Printf("Received message from Kafka: %s", message)

		// Extract the country
		parts := strings.Split(message, ", ")
		var country string
		for _, part := range parts {
			if strings.HasPrefix(part, "Country: ") {
				country = strings.TrimPrefix(part, "Country: ")
				break
			}
		}
		if country == "" {
			log.Printf("Could not parse country from message: %s", message)
			continue
		}

		// Increment the country counter in a hash (country_counts)
		err = redisClient.HIncrBy(context.Background(), "country_counts", country, 1).Err()
		if err != nil {
			log.Printf("Failed to increment country counter in Redis: %v", err)
			continue
		}

		// Increment the total messages counter in a hash (total_messages)
		err = redisClient.HIncrBy(context.Background(), "total_messages", "count", 1).Err()
		if err != nil {
			log.Printf("Failed to increment total messages in Redis: %v", err)
			continue
		}

		log.Printf("Updated Redis - Country: %s, Total Messages incremented", country)
	}
}
