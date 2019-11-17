#!/bin/bash
#сборка проекта
#веб интерфейс достпуен на localhost:81

#drop all containers
docker-compose down

#building server
docker build -t treegix_server:latest .

#building web UI
cd frontends
docker build -t treegix_web:latest .
cd ..

#start containers
docker-compose up -d frontend