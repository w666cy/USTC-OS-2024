#!/bin/bash
PS4='(run_test)$ '
set -x

# cd correct directory
cd "$(dirname "$0")"

./build_image.sh fat16.img

# build, mount simple_fat16 and test
fusermount -zu ./fat16
rm -rf ./fat16
mkdir -p ./fat16
make -C .. clean
make -C .. debug
../simple_fat16 -s ./fat16 --img="./fat16.img"

python3 -m pytest --capture=tee-sys -x -v ./fat16_test.py
fusermount -zu ./fat16
