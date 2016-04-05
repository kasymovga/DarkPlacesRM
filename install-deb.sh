#!/bin/bash

make clean
make sdl-nexuiz

sudo checkinstall -D make install
