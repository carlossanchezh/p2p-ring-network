FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive


RUN apt-get update && apt-get install -y --no-install-recommends gcc make libc6-dev && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY src/ /app/src/

RUN make -C /app/src

WORKDIR /app/src

CMD ["bash"]