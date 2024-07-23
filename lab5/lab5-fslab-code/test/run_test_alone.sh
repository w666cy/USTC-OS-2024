#!/bin/bash
PS4='(run_test)$ '
set -x

# cd correct directory
cd "$(dirname "$0")"

python3 -m pytest --capture=tee-sys -x -v ./fat16_test.py
fusermount -zu ./fat16
