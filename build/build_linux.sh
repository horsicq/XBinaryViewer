#!/bin/bash
# Build in system temporary folder
BUILD_DIR=$(mktemp -d -t xbinaryviewer-build-XXXXXXXXXX)
PROJECT_DIR=$(cd .. && pwd)

echo "Building in temporary directory: $BUILD_DIR"

cd "$BUILD_DIR"
cmake -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel "$PROJECT_DIR"
ninja
cpack

# Copy packages back to project
mkdir -p "$PROJECT_DIR/packages"
cp -Rf "$BUILD_DIR"/packages/* "$PROJECT_DIR/packages/" 2>/dev/null || true

# Cleanup
cd "$PROJECT_DIR"
rm -rf "$BUILD_DIR"
echo "Cleaned up temporary build directory"

