FROM gcc:7.5.0

RUN echo "deb http://archive.debian.org/debian/ buster main contrib non-free" > /etc/apt/sources.list && \
    echo "deb http://archive.debian.org/debian-security/ buster/updates main contrib non-free" >> /etc/apt/sources.list && \
    apt-get -o Acquire::Check-Valid-Until=false update && \
    apt-get -o Acquire::Check-Valid-Until=false upgrade -y

RUN apt-get -o Acquire::Check-Valid-Until=false install -y \
    wget \
    git \
    cmake \
    tar \
    make \
    ninja-build \
    python3 \
    python3-pip

RUN apt-get clean && \
    rm -rf /var/lib/apt/lists/*

RUN ln -s /usr/bin/python3 /usr/bin/python

WORKDIR /app
