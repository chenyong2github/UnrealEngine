Chromium Embedded Framework (CEF) Minimal Binary Distribution for Mac OS-X
-------------------------------------------------------------------------------

Date:             August 27, 2020

CEF Version:      84.4.0+g304e015+chromium-84.0.4147.105
CEF URL:          https://bitbucket.org/chromiumembedded/cef.git
                  @304e0155097a4576429fa27c8fa5a4c736f17d4b

Chromium Version: 84.0.4147.105
Chromium URL:     https://chromium.googlesource.com/chromium/src.git
                  @48466fd14f4d94f04d5039283ca706f868a6cc5f

This distribution contains the minimial components necessary to build and
distribute an application using CEF on the Mac OS-X platform. Please see
the LICENSING section of this document for licensing terms and conditions.


CONTENTS
--------

cmake       Contains CMake configuration files shared by all targets.

include     Contains all required CEF header files.

libcef_dll  Contains the source code for the libcef_dll_wrapper static library
            that all applications using the CEF C++ API must link against.

Release     Contains the "Chromium Embedded Framework.framework" and other
            components required to run the release version of CEF-based
            applications.


USAGE
-----

Building using CMake:
  CMake can be used to generate project files in many different formats. See
  usage instructions at the top of the CMakeLists.txt file.

Please visit the CEF Website for additional usage information.

https://bitbucket.org/chromiumembedded/cef/


REDISTRIBUTION
--------------

This binary distribution contains the below components. Components listed under
the "required" section must be redistributed with all applications using CEF.
Components listed under the "optional" section may be excluded if the related
features will not be used.

Applications using CEF on OS X must follow a specific app bundle structure.
Replace "cefclient" in the below example with your application name.

cefclient.app/
  Contents/
    Frameworks/
      Chromium Embedded Framework.framework/
        Chromium Embedded Framework <= main application library
        Libraries/
          libEGL.dylib <= angle support libraries
          libGLESv2.dylib <=^
          libswiftshader_libEGL.dylib <= swiftshader support libraries
          libswiftshader_libGLESv2.dylib <=^
        Resources/
          cef.pak <= non-localized resources and strings
          cef_100_percent.pak <====^
          cef_200_percent.pak <====^
          cef_extensions.pak <=====^
          devtools_resources.pak <=^
          icudtl.dat <= unicode support
          snapshot_blob.bin, v8_context_snapshot.bin <= V8 initial snapshot
          en.lproj/, ... <= locale-specific resources and strings
          Info.plist
      cefclient Helper.app/
        Contents/
          Info.plist
          MacOS/
            cefclient Helper <= helper executable
          Pkginfo
      Info.plist
    MacOS/
      cefclient <= cefclient application executable
    Pkginfo
    Resources/
      binding.html, ... <= cefclient application resources

The "Chromium Embedded Framework.framework" is an unversioned framework that
contains CEF binaries and resources. Executables (cefclient, cefclient Helper,
etc) must load this framework dynamically at runtime instead of linking it
directly. See the documentation in include/wrapper/cef_library_loader.h for
more information.

The "cefclient Helper" app is used for executing separate processes (renderer,
plugin, etc) with different characteristics. It needs to have a separate app
bundle and Info.plist file so that, among other things, it doesn't show dock
icons.

Required components:

The following components are required. CEF will not function without them.

* CEF core library.
  * Chromium Embedded Framework.framework/Chromium Embedded Framework

* Unicode support data.
  * Chromium Embedded Framework.framework/Resources/icudtl.dat

* V8 snapshot data.
  * Chromium Embedded Framework.framework/Resources/snapshot_blob.bin
  * Chromium Embedded Framework.framework/Resources/v8_context_snapshot.bin

Optional components:

The following components are optional. If they are missing CEF will continue to
run but any related functionality may become broken or disabled.

* Localized resources.
  Locale file loading can be disabled completely using
  CefSettings.pack_loading_disabled.

  * Chromium Embedded Framework.framework/Resources/*.lproj/
    Directory containing localized resources used by CEF, Chromium and Blink. A
    .pak file is loaded from this directory based on the CefSettings.locale
    value. Only configured locales need to be distributed. If no locale is
    configured the default locale of "en" will be used. Without these files
    arbitrary Web components may display incorrectly.

* Other resources.
  Pack file loading can be disabled completely using
  CefSettings.pack_loading_disabled.

  * Chromium Embedded Framework.framework/Resources/cef.pak
  * Chromium Embedded Framework.framework/Resources/cef_100_percent.pak
  * Chromium Embedded Framework.framework/Resources/cef_200_percent.pak
    These files contain non-localized resources used by CEF, Chromium and Blink.
    Without these files arbitrary Web components may display incorrectly.

  * Chromium Embedded Framework.framework/Resources/cef_extensions.pak
    This file contains non-localized resources required for extension loading.
    Pass the `--disable-extensions` command-line flag to disable use of this
    file. Without this file components that depend on the extension system,
    such as the PDF viewer, will not function.

  * Chromium Embedded Framework.framework/Resources/devtools_resources.pak
    This file contains non-localized resources required for Chrome Developer
    Tools. Without this file Chrome Developer Tools will not function.

* Angle support.
  * Chromium Embedded Framework.framework/Libraries/libEGL.dylib
  * Chromium Embedded Framework.framework/Libraries/libGLESv2.dylib
  Without these files HTML5 accelerated content like 2D canvas, 3D CSS and WebGL
  will not function.

* SwiftShader support.
  * Chromium Embedded Framework.framework/Libraries/libswiftshader_libEGL.dylib
  * Chromium Embedded Framework.framework/Libraries/libswiftshader_libGLESv2.dylib
  Without these files WebGL will not function in software-only mode when the GPU
  is not available or disabled.


LICENSING
---------

The CEF project is BSD licensed. Please read the LICENSE.txt file included with
this binary distribution for licensing terms and conditions. Other software
included in this distribution is provided under other licenses. Please visit
"about:credits" in a CEF-based application for complete Chromium and third-party
licensing information.
