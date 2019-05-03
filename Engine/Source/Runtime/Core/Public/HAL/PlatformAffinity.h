// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#if PLATFORM_XBOXONE
#include "XboxOne/XBoxOneAffinity.h"
#elif PLATFORM_PS4
#include "PS4/PS4Affinity.h"
#elif PLATFORM_LUMIN
#include "Lumin/LuminAffinity.h"
#elif PLATFORM_ANDROID
#include "Android/AndroidAffinity.h"
#else
#include COMPILED_PLATFORM_HEADER(PlatformAffinity.h)
#endif
