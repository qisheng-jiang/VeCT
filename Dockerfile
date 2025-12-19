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

RUN git config --global user.email "fake@fake.fake" && \
    git config --global user.name "fake name"

RUN pip3 install pandas matplotlib numpy 

WORKDIR /app
