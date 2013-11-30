#!/bin/bash

make clean
git pull
make
./cs352proxy config.txt