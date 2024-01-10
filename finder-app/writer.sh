#!/bin/bash

# Check if the correct number of arguments is provided
if [ "$#" -ne 2 ]; then
    echo "Error: Please provide both writefile and writestr as arguments."
    exit 1
fi

# Assign arguments to variables
writefile="$1"
writestr="$2"

# Extract the directory path from the full file path
dir_path=$(dirname "$writefile")

# Create the directory if it doesn't exist
mkdir -p "$dir_path"

# Check if the directory was created successfully
if [ $? -ne 0 ]; then
    echo "Error: Could not create the directory $dir_path."
    exit 1
fi

# Create the file with the specified content
echo "$writestr" > "$writefile"

# Check if the file was created successfully
if [ $? -ne 0 ]; then
    echo "Error: Could not create the file $writefile."
    exit 1
fi

echo "File $writefile created successfully with content: $writestwr"