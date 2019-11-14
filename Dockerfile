FROM os:latest

COPY * /app

RUN groupadd --system treegix
RUN useradd --system -g treegix -d /usr/lib/treegix -s /sbin/nologin -c "Treegix Monitoring System" treegix

RUN ./configure --enable-server  --with-postgresql
RUN make install