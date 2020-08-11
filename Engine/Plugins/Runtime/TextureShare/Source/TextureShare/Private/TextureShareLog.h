// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Plugin-wide log categories
#if UE_BUILD_SHIPPING
DECLARE_LOG_CATEGORY_EXTERN(LogTextureShare,    Warning, Warning);
DECLARE_LOG_CATEGORY_EXTERN(LogTextureShareBP,  Warning, Warning);
#else
DECLARE_LOG_CATEGORY_EXTERN(LogTextureShare,    Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogTextureShareBP,  Log, All);
#endif
