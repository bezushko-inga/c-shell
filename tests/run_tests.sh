#!/bin/bash

set -e

# Change shell.c to the actual filename of your shell source if needed.
gcc shell.c -o shell

for test in tests/*.in; do
    echo "Running $test"
    ./shell < "$test"
    echo
done
