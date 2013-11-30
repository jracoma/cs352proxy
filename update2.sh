#!/bin/bash

make clean
git pull
make
./cs352proxy config2.txt