// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if UE_BUILD_SHIPPING
DECLARE_LOG_CATEGORY_EXTERN(LogDisplayClusterPostprocess,              Warning, Warning);
DECLARE_LOG_CATEGORY_EXTERN(LogDisplayClusterPostprocessOutputRemap,   Warning, Warning);
DECLARE_LOG_CATEGORY_EXTERN(LogDisplayClusterPostprocessTextureShare,  Warning, Warning);
DECLARE_LOG_CATEGORY_EXTERN(LogDisplayClusterPostprocessD3D12CrossGPU, Warning, Warning);
#else
DECLARE_LOG_CATEGORY_EXTERN(LogDisplayClusterPostprocess,              Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogDisplayClusterPostprocessOutputRemap,   Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogDisplayClusterPostprocessTextureShare,  Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogDisplayClusterPostprocessD3D12CrossGPU, Log, All);
#endif
