package main

import (
	"context"
	"log"
	"strings"

	"github.com/redis/go-redis/v9"
	"github.com/streadway/amqp"
)

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
	q, err := ch.QueueDeclare(
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

	// Consume messages from RabbitMQ
	msgs, err := ch.Consume(
		q.Name, // queue
		"",     // consumer
		true,   // auto-ack
		false,  // exclusive
		false,  // no-local
		false,  // no-wait
		nil,    // args
	)
	if err != nil {
		log.Fatalf("Failed to register a consumer: %v", err)
	}

	// Connect to Valkey
	valkeyClient := redis.NewClient(&redis.Options{
		Addr: "valkey-service.proyecto2.svc.cluster.local:6379",
	})
	defer valkeyClient.Close()

	// Test Valkey connection
	_, err = valkeyClient.Ping(context.Background()).Result()
	if err != nil {
		log.Fatalf("Failed to connect to Valkey: %v", err)
	}

	log.Println("Connected to RabbitMQ and Valkey. Starting to consume messages...")

	// Consume messages from RabbitMQ and store in Valkey
	for msg := range msgs {
		// Parse the message (format: "Description: ..., Country: ..., Weather: ...")
		message := string(msg.Body)
		log.Printf("Received message from RabbitMQ: %s", message)

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
		err = valkeyClient.HIncrBy(context.Background(), "country_counts", country, 1).Err()
		if err != nil {
			log.Printf("Failed to increment country counter in Valkey: %v", err)
			continue
		}

		// Increment the total messages counter in a hash (total_messages)
		err = valkeyClient.HIncrBy(context.Background(), "total_messages", "count", 1).Err()
		if err != nil {
			log.Printf("Failed to increment total messages in Valkey: %v", err)
			continue
		}

		log.Printf("Updated Valkey - Country: %s, Total Messages incremented", country)
	}
}
