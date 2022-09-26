// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

//! A config file may have been provided with a define in the command-linewith something like
//! -D'MUTABLE_CONFIG="ConfigAndroid.h"'

#ifdef MUTABLE_CONFIG

#include MUTABLE_CONFIG

//! otherwise, use this one
#else

// This file contains all the possible configuration flags that affect the build of the runtime

//! Add support for OpenGL 4 gpu building operations.
//! If this is enabled, the .cpp files in the mutable/runtime/impl/gpu-gl4 need to be linked in the
//! runtime.
//#define MUTABLE_GPU_GL4

//! Add support for OpenGL ES 2 gpu building operations.
//! If this is enabled, the .cpp files in the mutable/runtime/impl/gpu-gles2 need to be linked in
//! the runtime.
//#define MUTABLE_GPU_GLES2

//! This flag enables collection of statistics in the System objects. If using the waf build system,
//! the statistics are only collected in the debug and profile versions of the runtime.
//#define MUTABLE_PROFILE

//! This flag disables the internal asserts of the library. Without this flag, the asserts are only
//! disabled in release builds. This can be overriden by MUTABLE_EXCEPTIONS
//#define MUTABLE_NO_ASSERT

//! This flags will enable asserts regardless of MUTABLE_NO_ASSERT but they will thrown an
//! exception instead of stop the program. This is useful for tools only.
//#define MUTABLE_EXCEPTIONS

//! Exclude code for multilayer image operations
//#define MUTABLE_DISABLE_MULTILAYER

//! Exclude support for BC(DXTC) image formats
//! This may also be set by the build system for some target platforms.
//#define MUTABLE_EXCLUDE_BC

//! Exclude support for ASTC image formats
//! This may also be set by the build system for some target platforms.
//#define MUTABLE_EXCLUDE_ASTC

//! Exclude support for higher quality compression image conversion (see Miro.h)
//! This may also be set by the build system for some target platforms.
//#define MUTABLE_EXCLUDE_IMAGE_COMPRESSION_QUALITY 4

#endif
