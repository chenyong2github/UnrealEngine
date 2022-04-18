// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetalCpp.h"

// Make the metal-cpp symbols visible to dependent modules
#pragma GCC visibility push(default)

#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION

#include "Foundation/Foundation.hpp"
#include "Metal/Metal.hpp"
#include "QuartzCore/QuartzCore.hpp"

#pragma GCC visibility pop
