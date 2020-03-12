FROM ubuntu
WORKDIR /hotstuff

COPY . /hotstuff

RUN sed -i s@/archive.ubuntu.com/@/mirrors.aliyun.com/@g /etc/apt/sources.list && apt-get update && apt-get install -y autoconf automake libtool build-essential cmake openssl libssl-dev libuv1-dev \
	&& rm CMakeCache.txt \
	&& cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED=ON -DHOTSTUFF_PROTO_LOG=ON \
	&& make

WORKDIR /hotstuff
ENTRYPOINT [ "./scripts/run_demo.sh" ]
