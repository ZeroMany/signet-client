### Automatic builds

Every push to GitHub builds the app for Windows and Linux (see
`.github/workflows/build.yml`). To download a build, open the repository on
GitHub, click **Actions**, pick a run, and download the file listed under
**Artifacts**.

The steps below are for building on your own computer.

### Get submodules

If you got this source from a Git repository then before you can build you must get the repository's submodules:

	$ git submodule update --init

If you downloaded the source from an archive then the submodules are already included.

### GNU/Linux build steps

On Debian-based systems (Ubuntu, Linux Mint, etc.), install Signet's build dependencies:

	$ sudo apt-get install qtbase5-dev qt5-qmake libqt5svg5-dev libqt5x11extras5-dev libgcrypt20-dev zlib1g-dev libx11-dev

On other systems, install the equivalent libraries for Qt5 (with Svg and X11Extras), zlib, libX11, and libgcrypt.

To build, run qmake and then make:

	$ qmake client/client.pro CONFIG+=release

	$ make

`make` will create a `signet` binary that you can move elsewhere.

You will need to install Signet's `udev` rule before the client will work. You can find the `udev` rule in `signet-base/signetdev/host/linux/50-signet.rules`. Use `sudo` to copy it to `/etc/udev/rules.d`:

	$ sudo cp signet-base/signetdev/host/linux/50-signet.rules /etc/udev/rules.d

### Mac OS build steps

To install Signet's build dependencies first install [Brew](https://brew.sh)

Once you have Brew, run:

	$ brew install qt5 libgcrypt libgpg-error zlib

To build Signet run:

	$ /usr/local/opt/qt5/bin/qmake client/client.pro CONFIG+=release
	$ make

The build will create a `signet.app` folder that you can move or copy anywhere. For example:

	$ cp -r signet.app ~/Desktop

The Mac build has not been tested since these instructions were last updated.

### Windows build steps

First install [MSYS2](https://www.msys2.org). MSYS2 will provide a build environment and a package manager to download Signet's dependencies. Once MSYS2 is installed, launch the "MSYS2 MINGW64" shell and run:

	$ pacman -S make mingw-w64-x86_64-gcc mingw-w64-x86_64-qt5-static mingw-w64-x86_64-zlib mingw-w64-x86_64-libgcrypt

Now you can build:

	$ export PATH=/mingw64/qt5-static/bin:$PATH
	$ qmake client/client.pro CONFIG+=release
	$ mingw32-make

This will build `signet.exe` in the `release` subdirectory. This executable is self-contained and you can copy it anywhere and run it.
