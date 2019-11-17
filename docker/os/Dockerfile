FROM ubuntu:18.04

COPY ./schema.sql /opt/schema.sql
COPY ./images.sql /opt/images.sql
COPY ./data.sql /opt/data.sql

RUN apt-get update && apt upgrade

#install apache2
RUN apt-get install -y apache2

#install postgres
#todo set Asia/Almaty
RUN echo "Asia/Almaty" | tee /etc/timezone
RUN apt-get install -y --no-install-recommends apt-utils
RUN echo 'debconf debconf/frontend select Noninteractive' | debconf-set-selections
RUN apt-get install tzdata
RUN dpkg-reconfigure -f noninteractive tzdata
RUN apt-get install -y postgresql-10

#create treegix user
RUN addgroup --system --quiet treegix
RUN adduser --quiet --system --disabled-login --ingroup treegix --home /var/lib/treegix --no-create-home treegix

#create treegix db and user
USER postgres
RUN mkdir -p /var/run/postgresql/10-main.pg_stat_tmp
#RUN /usr/lib/postgresql/10/bin/postgres -D /var/lib/postgresql/10/main -c config_file=/etc/postgresql/10/main/postgresql.conf >logfile 2>&1 &
RUN /usr/lib/postgresql/10/bin/pg_ctl -D /var/lib/postgresql/10/main start
RUN createuser treegix
RUN createdb -O treegix -E Unicode -T template0 treegix
RUN cat /opt/schema.sql | sudo -u treegix psql treegix
RUN cat /opt/images.sql | sudo -u treegix psql treegix
RUN cat /opt/data.sql | sudo -u treegix psql treegix

EXPOSE 80