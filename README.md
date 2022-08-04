Teeworlds-XolePanic
=========
DONT PANIC!THIS IS A TEEWORLDS INFECTION TEAM PVP MOD!

不要惊慌!这是一个Teeworlds感染团队PVP模式!

Building on Linux or macOS
在Linux和macOS上构建
==========================
Installing dependencies
安装前置库
-----------------------

    # Debian/Ubuntu 19.10+
    sudo apt install bam git libpnglite-dev libwavpack-dev python3
    
    # Fedora
    sudo dnf install bam gcc-c++ git pnglite-devel python3 wavpack-devel
    
    # Arch Linux (doesn't have pnglite in its repositories)
    sudo pacman -S --needed base-devel bam git python wavpack
    
    # macOS
    brew install bam
    
    # other 其他的 (add bam to your path, 添加bam到路径)
    git clone https://github.com/teeworlds/bam
    cd bam
    ./make_unix.sh

Building on Windows with MinGW
在windows上用MinGW构建
======================

Download and install MinGW with at least the following components:
下载MinGW(不能Mingw-64), 并安装以下包

- mingw-developer-toolkit-bin
- mingw32-base-bin
- mingw32-gcc-g++-bin
- msys-base-bin

Also install [Git](https://git-scm.com/downloads) (for downloading the source code), [Python](https://www.python.org/downloads/).

以及要安装 [Git](https://git-scm.com/downloads) (用于克隆源码), [Python](https://www.python.org/downloads/).

Build bam 0.5 构建bam0.5
-----------------------
    git clone git@github.com:matricks/bam.git
    cd bam
    ./make_win32_mingw.bat
