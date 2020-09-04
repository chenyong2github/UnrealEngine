// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Plugin-wide log categories
#if UE_BUILD_SHIPPING
DECLARE_LOG_CATEGORY_EXTERN(LogTextureShareD3D11,    Warning, Warning);
#else
DECLARE_LOG_CATEGORY_EXTERN(LogTextureShareD3D11,    Log, All);
#endif
