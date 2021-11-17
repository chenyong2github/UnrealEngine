oodle2_readme.txt

Welcome to Oodle!

If you have any questions or problems, please email oodle@radgametools.com

To get started, open help/oodle.chm and look for "Quick Start Guide"

Oodle now comes in 3 SDKs which are licensed separately.  They are independent and can be used 
separately or together.

They are :

Oodle Data : lossless generic data compression (Kraken, Leviathan, Mermaid & Selkie)
Oodle Network : Network packet compression for online games
Oodle Texture : GPU compressed texture encoders and transforms for BCN block textures

---------------------------------------------------------------
What's in this distribution :
 (not all platforms include all these directories)

[help]
  oodle2.html  - the main help , start here! (Data and Network help is here)
  oodle2tex.html  - Oodle2 Texture SDK help

[bin]
  for Oodle Data :
  example_lz_chart.exe : test lots of oodle compressors on your file (with source code)
  ozip.exe : use ozip -b for benchmarking (with source code)

  for Oodle Texture :
  texviz.exe : Windows texture visualizer
  otexdds.exe : command line DDS texture processor (with source code)

  non-windows exes will need chmod +x

[examples]
  examples of using Oodle

[include]
  oodle2.h  : Oodle2 Data Compression "core" header
  oodle2x.h : Oodle2 Data Compression "extras" header (threads, IO, requires init)
  oodle2net.h : in Oodle2 Network SDK
  oodle2tex.h : in Oodle2 Texture SDK (for tools)
  oodle2texrt.h : in Oodle2 Texture SDK (for runtime)
  oodle2base.h : Oodle2 shared base header; all the other public headers include this

[lib]
  dynamic libs and/or static libs for your platform

[static_lib] (Microsoft only)
  on Microsoft platforms the lib/ dir are DLL import libs; the static libs are here

[redist]
  redistributable files you may ship with your game
  (the Oodle DLL on Windows)
   on non-Windows platforms, the dynamic libs (so,dylib) may be redistributed. 
  no other files from the Oodle SDK should ever be redistributed!

[redistdebug]
  debug versions of the redist (DLL) files
  do NOT distribute these with your game!

---------------------------------------------------------------
Install dirs :

Each Oodle platform SDK comes as a complete separate SDK.

They are normally installed into a dir for each platform, with a full SDK tree in each platform :

android
ios
linux
linuxarm
mac
ps4
switch
win
winuwp
xboxone

containing

android/help
android/examples
etc.
for each platform

This means some identical files will be copied in each dir.

The Oodle headers are the same on every platform, you can include just one of them.

The Oodle2 Data, Network, and Texture SDK's can be installed on top of each other to the same dir.
Some files are in all the SDKS, you can overwrite them.  So for example you could take the
Oodle Data and Texture SDKs for Windows and PS5 and install them on top of each other.  Files with
the same name are not different between the SDKs.  They can also be used installed to different 
directories.

---------------------------------------------------------------
A note on dynamic library versions and compatibility :

The Oodle version number is :

2.MAJOR.MINOR

The shared libraries (DLL,so,dylib) are named with the MAJOR version number in the name.

eg. Oodle 2.8.x will use the DLL oo2core_8_win64.dll

Whenever the API changes, creating a binary link incompatibility, the MAJOR version number is 
incremented.

The MINOR version number is incremented for bug fixes and performance improvements that do not 
cause incompatibility.  This means you can update dynamic libraries for minor version number changes
on compiled games.

---------------------------------------------------------------
Choosing your build :

If you have link incompatibility issues due to CRT/libc or compiler setting conflicts, try the
dynamic libs (DLL,so,dylib) as they are less likely to have those issues.

On non-Microsoft platforms, the static and dynamic libs are both in "lib/" and you can choose
which ones to link.

On Microsoft platforms, the "lib/" folder contains DLL import libs.  To link with the static
libs on Microsoft platforms, use "static_lib/" instead.

The Oodle "Core" library is nearly OS version independent.  It uses a bare minimum of system
dependencies, it tries to take all of its system linkage through function pointers, which you
can change via the "Plugins" APIs.  The "Ext" library contains all the system calls and is much
more likely to break with a host SDK version incompatibility.  If you encounter SDK link
incompatibilities in "Ext", try linking with "Core" only.  It is recommeneded to use Oodle Core
only in your shipping game runtime.

You should generally link with the release build of Oodle even in your debug build.  The debug
version of Oodle is provided to get more information if you believe you have a problem inside Oodle.

The debug builds of the dynamic libs should not be distributed.  The Oodle libraries often contain
debug information and symbols.  This should be stripped from any EXE you link with Oodle before
distribution.  All Oodle debug information and symbols are not for redistribution.

The release builds of the dynamic libraries (.dll,.so,.dylib) may be redistributed.

---------------------------------------------------------------
Walkthrough Videos :

There are a couple walkthrough videos for the Oodle SDK we've posted on Youtube.

Welcome to Oodle Texture 2.8.8 06-30-2020 : 

https://www.youtube.com/watch?v=zTCWRqfI-No

Oodle Network Compression Unreal Integration Walkthrough 06-29-2020 Oodle 2.8.7 Unreal 4.25.1 :

https://www.youtube.com/watch?v=JeMaslp3RYU

Oodle Data Compression Unreal Integration Walkthrough 06-29-2020 Oodle 2.8.7 Unreal 4.25.1 :

https://www.youtube.com/watch?v=VgBRskY0rFs


