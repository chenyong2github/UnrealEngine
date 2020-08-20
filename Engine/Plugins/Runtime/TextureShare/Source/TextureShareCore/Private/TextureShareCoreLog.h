// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Plugin-wide log categories
#if UE_BUILD_SHIPPING
DECLARE_LOG_CATEGORY_EXTERN(LogTextureShareCore,    Warning, Warning);
DECLARE_LOG_CATEGORY_EXTERN(LogTextureShareCoreD3D, Warning, Warning);
#else
DECLARE_LOG_CATEGORY_EXTERN(LogTextureShareCore,    Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogTextureShareCoreD3D, Log, All);
#endif
