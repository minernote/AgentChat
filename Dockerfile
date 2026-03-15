FROM ubuntu:22.04

LABEL description="AgentChat server"
LABEL maintainer="AgentChat project"

# Avoid interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        cmake \
        g++ \
        libssl-dev \
        libsqlite3-dev \
        make \
        ca-certificates && \
    rm -rf /var/lib/apt/lists/*

COPY . /app
WORKDIR /app

RUN mkdir -p build && \
    cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

EXPOSE 8765
EXPOSE 8766

CMD ["/app/build/agentchat_server", "--port", "8765", "--ws-port", "8766"]
