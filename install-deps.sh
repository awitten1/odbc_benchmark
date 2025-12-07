#!/bin/bash

set -eux

sudo apt update
sudo apt install -y unixodbc unixodbc-dev odbc-postgresql postgresql

