FROM golang:1.23 AS builder
WORKDIR /app
COPY . .
RUN go mod download
RUN CGO_ENABLED=0 GOOS=linux go build -o rabbitmq-consumer main.go

FROM alpine:latest
RUN apk --no-cache add ca-certificates
COPY --from=builder /app/rabbitmq-consumer /rabbitmq-consumer
CMD ["/rabbitmq-consumer"]