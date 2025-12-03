# Compiling MiSTer Main Binary

This document describes how to compile the Main MiSTer binary.

## Option 1: Docker (Recommended - Cross-Platform)

### Using MiSTer-docker-build

```bash
# Clone the docker build tools
git clone https://github.com/hunson-abadeer/MiSTer-docker-build.git
cd MiSTer-docker-build

# Compile Main_MiSTer (run from the Main_MiSTer directory)
./mister_arm_compile.sh make
```

### Using Toolchain_MiSTer Docker Image

```bash
# Pull the docker image
docker pull misterkun/toolchain

# Run compilation from Main_MiSTer directory
docker run --rm -v $(pwd):/workdir misterkun/toolchain make
```

## Option 2: Native Linux/WSL2 (Ubuntu 20.04+ recommended)

### Prerequisites

```bash
# Update system and install dependencies
sudo apt update && sudo apt upgrade -y
sudo apt-get install build-essential git libncurses-dev flex bison openssl \
    libssl-dev dkms libelf-dev libudev-dev libpci-dev libiberty-dev autoconf \
    liblz4-tool bc curl gcc git libssl-dev libncurses5-dev lzop make \
    u-boot-tools libgmp3-dev libmpc-dev
```

### Download ARM Cross-Compiler

```bash
# Download the ARM GCC 10.2 toolchain
wget -c https://developer.arm.com/-/media/Files/downloads/gnu-a/10.2-2020.11/binrel/gcc-arm-10.2-2020.11-x86_64-arm-none-linux-gnueabihf.tar.xz

# Extract to /opt (requires sudo)
sudo tar xf gcc-arm-10.2-2020.11-x86_64-arm-none-linux-gnueabihf.tar.xz -C /opt
```

### Compile

```bash
# Set PATH to include the ARM toolchain
export PATH=/opt/gcc-arm-10.2-2020.11-x86_64-arm-none-linux-gnueabihf/bin:$PATH

# Navigate to Main_MiSTer directory and compile
cd /path/to/Main_MiSTer
make clean
make -j$(nproc)
```

The compiled binary will be at `bin/MiSTer`.

## Quick One-Liner (after setup)

```bash
PATH=/opt/gcc-arm-10.2-2020.11-x86_64-arm-none-linux-gnueabihf/bin:$PATH make -j$(nproc)
```

## Output

After successful compilation:
- `bin/MiSTer` - The main binary (stripped)
- `bin/MiSTer.elf` - Debug version with symbols

## Deploying to MiSTer

Copy the compiled binary to your MiSTer's SD card:

```bash
# Via SCP (replace with your MiSTer's IP)
scp bin/MiSTer root@192.168.x.x:/media/fat/MiSTer

# Or via network share
cp bin/MiSTer /path/to/mister/share/
```

Then reboot your MiSTer or run the new binary manually.

## References

- [MiSTer FPGA Documentation - Compiling](https://mister-devel.github.io/MkDocs_MiSTer/developer/mistercompile/)
- [Main_MiSTer Wiki](https://github.com/MiSTer-devel/Main_MiSTer/wiki)
- [MiSTer-docker-build](https://github.com/hunson-abadeer/MiSTer-docker-build)
- [Toolchain_MiSTer](https://github.com/misterkun-io/Toolchain_MiSTer)
