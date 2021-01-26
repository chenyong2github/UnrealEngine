# usbmuxd-win32

*A socket daemon to multiplex connections from and to iOS devices.*

## Features
[![Build Status](https://dev.azure.com/libimobiledevice-win32/imobiledevice-net/_apis/build/status/libimobiledevice-win32.usbmuxd)](https://dev.azure.com/libimobiledevice-win32/imobiledevice-net/_build/latest?definitionId=6)

Provides a native Windows build (using the Visual C++ compiler) of [usbmuxd](http://libimobiledevice.org),
a socket daemon to multiplex connections from and to iOS devices.

> **NOTE**: This is work in progress; usbmuxd requires special USB drivers which work with libusb and are able to select a configuration which is not the default configuration. These drivers are not available yet.

## Background

usbmuxd stands for "USB multiplexing daemon". This daemon is in charge of
multiplexing connections over USB to an iOS device.

To users, it means you can use various applications to interact with your
device.

To developers, it means you can connect to any listening localhost socket on
the device.

Some key features are:

- **Implementation**: Open-Source implementation of proprietary usbmuxd daemon
- **Cross-Platform:** Tested on Linux, macOS, Windows and Android platforms
- **Linux**: Supports udev and systemd for automatic activation
- **Compatibility**: Supports latest device firmware releases
- **Scalability**: Supports multiple connections to different ports in parallel

usbmuxd is not used for tethering data transfers which uses a dedicated USB
interface to act as a virtual network device.

The higher-level layers, especially if you want to write an application to
interact with the device, are handled by [libimobiledevice](https://github.com/libimobiledevice/libimobiledevice.git).

The low-level layer is handled by [libusbmuxd](https://github.com/libimobiledevice/libusbmuxd.git).

## Installation / Getting started

### Debian / Ubuntu Linux

First install all required dependencies and build tools:
```shell
sudo apt-get install \
	build-essential \
	checkinstall \
	git \
	autoconf \
	automake \
	libtool-bin \
	libplist-dev \
	libusbmuxd-dev \
	libimobiledevice-dev \
	libusb-1.0-0-dev \
	udev
```

If systemd is not installed and should control spawning the daemon use:
```shell
sudo apt-get install \
	systemd
```

Then clone the actual project repository:
```shell
git clone https://github.com/libimobiledevice/usbmuxd.git
cd usbmuxd
```

Now you can build and install it:
```shell
./autogen.sh
make
sudo make install
```

If you require a custom prefix or other option being passed to `./configure`
you can pass them directly to `./autogen.sh` like this:
```bash
./autogen.sh --prefix=/opt/local --without-preflight --without-systemd
make
sudo make install
```

To output a list of available configure options use:
```bash
./autogen.sh --help
```

## Usage

The daemon is automatically started by udev or systemd depending on what you
have configured upon hotplug of an iOS device and exits if the last device
was unplugged.

When usbmuxd is running it provides a socket interface at `/var/run/usbmuxd`
that is designed to be compatible with the socket interface that is provided
on macOS.

You should also create an `usbmux` user that has access to USB devices on your
system. Alternatively, just pass a different username using the `-U` argument.

The daemon also manages pairing records with iOS devices and the host in
`/var/lib/lockdown` (Linux) or `/var/db/lockdown` (macOS).

Ensure proper permissions are setup for the daemon to access the directory.

For debugging purposes it is helpful to start usbmuxd using the foreground `-f`
argument and enable verbose mode `-v` to get suitable logs.

Please consult the usage information or manual page for a full documentation of
available command line options:
```shell
usbmuxd --help
man usbmuxd
```

## Contributing

We welcome contributions from anyone and are grateful for every pull request!

If you'd like to contribute, please fork the `master` branch, change, commit and
send a pull request for review. Once approved it can be merged into the main
code base.

If you plan to contribute larger changes or a major refactoring, please create a
ticket first to discuss the idea upfront to ensure less effort for everyone.

Please make sure your contribution adheres to:
* Try to follow the code style of the project
* Commit messages should describe the change well without being to short
* Try to split larger changes into individual commits of a common domain
* Use your real name and a valid email address for your commits

We are still working on the guidelines so bear with us!

## Links

* Homepage: https://libimobiledevice.org/
* Repository: https://git.libimobiledevice.org/usbmuxd.git
* Repository (Mirror): https://github.com/libimobiledevice/usbmuxd.git
* Issue Tracker: https://github.com/libimobiledevice/usbmuxd/issues
* Mailing List: https://lists.libimobiledevice.org/mailman/listinfo/libimobiledevice-devel
* Twitter: https://twitter.com/libimobiledev

## License

This library and utilities are licensed under the [GNU General Public License v3.0](https://www.gnu.org/licenses/gpl-3.0.en.html),
also included in the repository in the `COPYING.GPLv3` file.

## Credits

The initial usbmuxd daemon implementation was authored by Hector Martin.

Apple, iPhone, iPad, iPod, iPod Touch, Apple TV, Apple Watch, Mac, iOS,
iPadOS, tvOS, watchOS, and macOS are trademarks of Apple Inc.

usbmuxd is an independent software application and has not been
authorized, sponsored, or otherwise approved by Apple Inc.

README Updated on: 2020-06-13

For general questions about usbmuxd, see http://github.com/libimobiledevice/usbmuxd.
For questions specific to Visual C++, feel free to use the GitHub issue tracker

## How to get the latest binaries

The binaries for usbmuxd are added as an artifact to each Azure Pipeline build. Check the status of the [latest build](https://dev.azure.com/libimobiledevice-win32/imobiledevice-net/_apis/build/status/libimobiledevice-win32.usbmuxd) and download the .zip file.

On Windows, you'll need to install the following dependencies:
* [Microsoft Visual C++ Redistributable Packages for Visual Studio 2015](https://www.microsoft.com/en-us/download/details.aspx?id=48145)
* [Microsoft Visual C++ Redistributable Packages for Visual Studio 2012 Update 4](http://www.microsoft.com/en-us/download/details.aspx?id=30679)

## Building on Windows
You can open `usbmuxd.sln` in Visual Studio and restore the packages and build from there, or from the commandline:
```
nuget restore
msbuild usbmuxd.sln
```

## Building on Ubuntu
Compatibility with Linux is important, so here's how you can build on Ubuntu 16.04.

Make sure you've built and installed `libplist` and `libusbmuxd` first.

```
apt-get install libusb-1.0-0 libusb-1.0-0-dev
./autogen.sh
make
sudo make install
```

## Consulting, Training and Support


This repository is maintained by [Quamotion](http://quamotion.mobi). Quamotion develops test software for iOS and

Android applications, based on the WebDriver protocol.



Quamotion offers various technologies related to automating iOS devices using

computers running Windows or Linux.

This includes:

* The ability to remotely control iOS devices

* Extensions to libimobiledevice with support for the Instruments protocol

* Running Xcode UI Tests and Facebook WebDriverAgent tests



In certain  cases, Quamotion also offers professional services - such as consulting, training and support - related

to imobiledivice-net and libimobiledevice.



Contact us at [info@quamotion.mobi](mailto:info@quamotion.mobi) for more information.