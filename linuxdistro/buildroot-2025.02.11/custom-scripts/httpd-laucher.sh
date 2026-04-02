#!/bin/bash

cd "/var/www" || exit 1

httpd -f &
PID=$!
echo $PID > /var/run/httpd_background.pid