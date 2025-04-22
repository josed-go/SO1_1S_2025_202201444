FROM golang:1.23 AS builder
WORKDIR /app
COPY . .
RUN go mod download
RUN CGO_ENABLED=0 GOOS=linux go build -o kafka-consumer main.go

FROM alpine:latest
RUN apk --no-cache add ca-certificates
COPY --from=builder /app/kafka-consumer /kafka-consumer
CMD ["/kafka-consumer"]