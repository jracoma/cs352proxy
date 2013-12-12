#!/bin/bash

make clean
git pull
make cs352proxy1
./cs352proxy1 config3.txt