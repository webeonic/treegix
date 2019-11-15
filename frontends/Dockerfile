FROM php:7.3-apache
RUN apt-get update && apt-get -y upgrade

RUN apt-get install -y libpng-dev libjpeg-dev libpq-dev libfreetype6-dev
RUN docker-php-ext-configure gd --with-freetype-dir=/usr/include/ --with-jpeg-dir=/usr/include/ --with-png-dir=/usr/include/
RUN docker-php-ext-install  sockets gd bcmath pgsql

COPY treegix.conf.php /var/www/html/conf/treegix.conf.php
COPY php.ini $PHP_INI_DIR/php.ini
COPY php/ /var/www/html
