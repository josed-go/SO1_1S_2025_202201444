FROM golang:1.23 AS builder
WORKDIR /app
COPY . .
RUN go mod download
RUN CGO_ENABLED=0 GOOS=linux go build -o go-rabbitmq-writer main.go

FROM alpine:latest
RUN apk --no-cache add ca-certificates
COPY --from=builder /app/go-rabbitmq-writer /go-rabbitmq-writer
EXPOSE 50051
CMD ["/go-rabbitmq-writer"]