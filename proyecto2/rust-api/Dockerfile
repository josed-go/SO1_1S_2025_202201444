# Use a Rust base image for building
FROM rust:1.81 AS builder

WORKDIR /usr/src/rust-api
COPY . .
RUN cargo build --release

# Use a smaller base image for the final container
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y libssl-dev ca-certificates && rm -rf /var/lib/apt/lists/*
COPY --from=builder /usr/src/rust-api/target/release/rust-api /usr/local/bin/rust-api
EXPOSE 8080
CMD ["rust-api"]