#!/bin/bash

make clean
git pull
make
./cs352proxy config3.txt