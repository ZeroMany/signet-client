### Automatic builds

Every pull request and every change to `main` builds the app for Windows,
Linux and Mac (see `.github/workflows/ci.yml`). To download a build, open the repository on
GitHub, click **Actions**, pick a run, and download the file listed under
**Artifacts**.

The steps below are for building on your own computer.

### Get submodules

If you got this source from a Git repository then before you can build you must get the repository's submodules:

	$ git submodule update --init

If you downloaded the source from an archive then the submodules are already included.

### GNU/Linux build steps

On Debian-based systems (Ubuntu, Linux Mint, etc.), install Signet's build dependencies:

	$ sudo apt-get install qtbase5-dev qt5-qmake libqt5svg5-dev libqt5x11extras5-dev libgcrypt20-dev zlib1g-dev libx11-dev libpcsclite-dev

On other systems, install the equivalent libraries for Qt5 (with Svg and X11Extras), zlib, libX11, libgcrypt and pcsc-lite.

To use a YubiKey with Signet, the smart-card service must also be installed and running (see [YUBIKEY.md](YUBIKEY.md)):

	$ sudo apt-get install pcscd
	$ sudo systemctl enable --now pcscd.socket

To build, run qmake and then make:

	$ qmake client/client.pro CONFIG+=release

	$ make

`make` will create a `signet` binary that you can move elsewhere.

You will need to install Signet's `udev` rule before the client will work. You can find the `udev` rule in `signet-base/signetdev/host/linux/50-signet.rules`. Use `sudo` to copy it to `/etc/udev/rules.d`:

	$ sudo cp signet-base/signetdev/host/linux/50-signet.rules /etc/udev/rules.d

### Mac OS build steps

These steps are for Macs with Apple M-chips (M1 and newer). Intel Macs are
no longer supported. The finished app needs macOS 14 Sonoma or newer.

First install [Homebrew](https://brew.sh). Then run:

	$ brew install qt@5 libgcrypt

To build Signet run:

	$ export PATH="$(brew --prefix qt@5)/bin:$PATH"
	$ qmake client/client.pro CONFIG+=release
	$ make
	$ macdeployqt signet.app
	$ codesign --force --deep --sign - signet.app

The build creates a `signet.app` folder that you can move to Applications.
Because the app isn't signed by Apple, the first time you open it macOS
will refuse. Open **System Settings → Privacy & Security**, scroll down,
and click **Open Anyway** next to the message about Signet.

### Windows build steps

First install [MSYS2](https://www.msys2.org). MSYS2 will provide a build environment and a package manager to download Signet's dependencies. Once MSYS2 is installed, launch the "MSYS2 MINGW64" shell and run:

	$ pacman -S mingw-w64-x86_64-make mingw-w64-x86_64-gcc mingw-w64-x86_64-qt5-static mingw-w64-x86_64-zlib mingw-w64-x86_64-libgcrypt mingw-w64-x86_64-openssl mingw-w64-x86_64-zstd

Now you can build:

	$ export PATH=/mingw64/qt5-static/bin:$PATH
	$ qmake client/client.pro CONFIG+=release
	$ mingw32-make

This will build `signet.exe` in the `release` subdirectory. This executable is self-contained and you can copy it anywhere and run it.

### Running the tests

The YubiKey unlock feature has its own tests, which use a pretend YubiKey
and a pretend Signet, so they need no hardware. Build and run them from a
separate folder (on Windows use `mingw32-make` and `release/hwkey_tests.exe`):

	$ mkdir build-tests && cd build-tests
	$ qmake ../client/tests/hwkey/hwkey_tests.pro CONFIG+=release
	$ make
	$ ./hwkey_tests

The automatic builds run these tests on Windows, Linux and Mac.
