#!/bin/bash

# Build script for ShellKil project
# Alternative to Makefile for systems without make

set -e  # Exit on error

echo "Building ShellKil project..."

# Compiler and flags
CC="g++"
CFLAGS="-Wall -Wextra -std=c++14 -O2 -g"
LDFLAGS="-lreadline"

# Create directories
mkdir -p obj bin

echo "Compiling object files..."

# Compile object files
$CC $CFLAGS -c shell.cpp -o obj/shell.o
$CC $CFLAGS -c delep.cpp -o obj/delep.o
$CC $CFLAGS -c history.cpp -o obj/history.o
$CC $CFLAGS -c squashbug.cpp -o obj/squashbug.o

echo "Linking main executable..."

# Link main executable
$CC $CFLAGS -o bin/shellkil obj/shell.o obj/delep.o obj/history.o obj/squashbug.o $LDFLAGS

echo "Building utilities..."

# Build utilities
$CC $CFLAGS -o bin/createlock createlock.cpp
$CC $CFLAGS -o bin/test_squashbug test_squashbug.cpp squashbug.cpp
$CC $CFLAGS -o bin/nolock nolock.cpp

echo "Build completed successfully!"
echo "Executables are in the bin/ directory:"
echo "  - bin/shellkil (main shell)"
echo "  - bin/createlock (file locking test)"
echo "  - bin/test_squashbug (malware simulation)"
echo "  - bin/nolock (file access test)"
echo
echo "To run the shell: ./bin/shellkil" 