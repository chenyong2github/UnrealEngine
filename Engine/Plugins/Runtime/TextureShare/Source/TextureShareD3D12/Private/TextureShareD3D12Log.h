// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

// Plugin-wide log categories
#if UE_BUILD_SHIPPING
DECLARE_LOG_CATEGORY_EXTERN(LogTextureShareD3D12,    Warning, Warning);
DECLARE_LOG_CATEGORY_EXTERN(LogD3D12CrossGPUHeap, Warning, Warning);
#else
DECLARE_LOG_CATEGORY_EXTERN(LogTextureShareD3D12,    Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogD3D12CrossGPUHeap, Log, All);
#endif
