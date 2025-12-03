#!/bin/bash
#
# MiSTer Main Binary Compile Script
# Supports both native toolchain and Docker
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLCHAIN_PATH="/opt/gcc-arm-10.2-2020.11-x86_64-arm-none-linux-gnueabihf/bin"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
echo_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
echo_error() { echo -e "${RED}[ERROR]${NC} $1"; }

usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --docker       Use Docker for compilation (recommended for cross-platform)"
    echo "  --native       Use native ARM toolchain (requires toolchain installed)"
    echo "  --clean        Clean before building"
    echo "  --debug        Build with debug symbols"
    echo "  --help         Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0 --docker           # Compile using Docker"
    echo "  $0 --native --clean   # Clean and compile using native toolchain"
}

compile_docker() {
    echo_info "Compiling with Docker..."

    if ! command -v docker &> /dev/null; then
        echo_error "Docker is not installed or not in PATH"
        exit 1
    fi

    # Check if docker daemon is running
    if ! docker info &> /dev/null; then
        echo_error "Docker daemon is not running"
        exit 1
    fi

    echo_info "Pulling toolchain image..."
    docker pull misterkun/toolchain

    if [ "$CLEAN" = "1" ]; then
        echo_info "Cleaning..."
        docker run --rm -v "$SCRIPT_DIR":/workdir misterkun/toolchain make clean
    fi

    echo_info "Building..."
    if [ "$DEBUG" = "1" ]; then
        docker run --rm -v "$SCRIPT_DIR":/workdir misterkun/toolchain make DEBUG=1 -j$(nproc)
    else
        docker run --rm -v "$SCRIPT_DIR":/workdir misterkun/toolchain make -j$(nproc)
    fi
}

compile_native() {
    echo_info "Compiling with native toolchain..."

    # Check for toolchain
    if [ ! -d "$TOOLCHAIN_PATH" ]; then
        echo_error "ARM toolchain not found at $TOOLCHAIN_PATH"
        echo_info "Download it with:"
        echo "  wget -c https://developer.arm.com/-/media/Files/downloads/gnu-a/10.2-2020.11/binrel/gcc-arm-10.2-2020.11-x86_64-arm-none-linux-gnueabihf.tar.xz"
        echo "  sudo tar xf gcc-arm-10.2-2020.11-x86_64-arm-none-linux-gnueabihf.tar.xz -C /opt"
        exit 1
    fi

    export PATH="$TOOLCHAIN_PATH:$PATH"

    # Verify compiler works
    if ! arm-none-linux-gnueabihf-gcc --version &> /dev/null; then
        echo_error "ARM compiler not working"
        exit 1
    fi

    cd "$SCRIPT_DIR"

    if [ "$CLEAN" = "1" ]; then
        echo_info "Cleaning..."
        make clean
    fi

    echo_info "Building..."
    if [ "$DEBUG" = "1" ]; then
        make DEBUG=1 -j$(nproc)
    else
        make -j$(nproc)
    fi
}

# Parse arguments
USE_DOCKER=0
USE_NATIVE=0
CLEAN=0
DEBUG=0

while [[ $# -gt 0 ]]; do
    case $1 in
        --docker)
            USE_DOCKER=1
            shift
            ;;
        --native)
            USE_NATIVE=1
            shift
            ;;
        --clean)
            CLEAN=1
            shift
            ;;
        --debug)
            DEBUG=1
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo_error "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

# Default to Docker if available, otherwise native
if [ "$USE_DOCKER" = "0" ] && [ "$USE_NATIVE" = "0" ]; then
    if command -v docker &> /dev/null && docker info &> /dev/null 2>&1; then
        echo_info "Auto-detected Docker, using Docker compilation"
        USE_DOCKER=1
    elif [ -d "$TOOLCHAIN_PATH" ]; then
        echo_info "Auto-detected native toolchain, using native compilation"
        USE_NATIVE=1
    else
        echo_error "No compilation method available!"
        echo_info "Either install Docker or the ARM toolchain"
        echo_info "Run '$0 --help' for more information"
        exit 1
    fi
fi

# Run compilation
if [ "$USE_DOCKER" = "1" ]; then
    compile_docker
else
    compile_native
fi

# Check result
if [ -f "$SCRIPT_DIR/bin/MiSTer" ]; then
    echo ""
    echo_info "Compilation successful!"
    echo_info "Binary location: $SCRIPT_DIR/bin/MiSTer"
    ls -lh "$SCRIPT_DIR/bin/MiSTer"
else
    echo_error "Compilation failed - binary not found"
    exit 1
fi
