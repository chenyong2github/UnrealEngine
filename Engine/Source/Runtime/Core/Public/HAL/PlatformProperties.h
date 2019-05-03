// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"
#include "GenericPlatform/GenericPlatformProperties.h"

// note that this is not defined to 1 like normal, because we don't want to have to define it to 0 whenever
// the Properties.h files are included in all other places, so just use #ifdef not #if in this special case
#define PROPERTY_HEADER_SHOULD_DEFINE_TYPE

#if PLATFORM_PS4
#include "PS4/PS4Properties.h"
#elif PLATFORM_XBOXONE
#include "XboxOne/XboxOneProperties.h"
#elif PLATFORM_ANDROID
#include "Android/AndroidProperties.h"
#else
#include COMPILED_PLATFORM_HEADER(PlatformProperties.h)
#endif

#undef PROPERTY_HEADER_SHOULD_DEFINE_TYPE
