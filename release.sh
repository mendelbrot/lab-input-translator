#!/bin/bash

# A release is a stable version of the code for deployment in the lab.
# This script copies the hid.uf2 and msc.uf2 binary files to the release-bin directory.
# The first arguement is the mandatory release name (date or version).
# The release name must be a valid folder name.
# Usage example: ./release.sh 2025-11-04

# Check if an argument is provided
if [ $# -lt 1 ]; then
   echo "Missing argument: Please privide the release name (date or version)."
   exit 1
fi

# Store the name argument
name=$1

if ! [[ "$name" != "." && "$name" != ".." && "$name" =~ ^[a-zA-Z0-9._-]+$ ]]; then
    echo "Invalid arguement: The release name must be alphanumeric, with only dot, underscore, or dash special characters permitted."
    exit 1
fi

# Create the release directory if it doesn't exist
mkdir -p "release-bin/$name"

# Copy the uf2 files
cp "hid/build/hid.uf2" "release-bin/$name/"
cp "msc/build/msc.uf2" "release-bin/$name/"

echo "Binaries copied to release-bin/$name/"
