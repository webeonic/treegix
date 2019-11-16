FROM ubuntu:18.04

COPY . /app/

RUN groupadd --system treegix
RUN useradd --system -g treegix -d /usr/lib/treegix -s /sbin/nologin -c "Treegix Monitoring System" treegix

RUN apt-get update && apt-get upgrade
RUN apt-get install -y gcc \
 libpq-dev \
  zlib1g-dev \
   libevent-dev \
    libpcre3-dev \
     make \
      autoconf \
       automake

WORKDIR /app
RUN ./configure --enable-server  --with-postgresql
RUN make install

CMD ["/usr/local/sbin/treegix_server"]