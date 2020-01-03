// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Plugin-wide log categories
#if UE_BUILD_SHIPPING
DECLARE_LOG_CATEGORY_EXTERN(LogPicpMPCDI, Warning, Warning);
#else
DECLARE_LOG_CATEGORY_EXTERN(LogPicpMPCDI, Log, All);
#endif
