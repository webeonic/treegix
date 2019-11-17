#!/bin/bash
cd frontends
docker build -t treegix_web:latest .
cd ..
docker-compose stop frontend
docker-compose up -d frontend