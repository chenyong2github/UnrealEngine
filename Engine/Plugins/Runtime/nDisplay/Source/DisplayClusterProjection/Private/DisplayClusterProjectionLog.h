// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if UE_BUILD_SHIPPING
DECLARE_LOG_CATEGORY_EXTERN(LogDisplayClusterProjection,          Warning, Warning);
DECLARE_LOG_CATEGORY_EXTERN(LogDisplayClusterProjectionCamera,    Warning, Warning);
DECLARE_LOG_CATEGORY_EXTERN(LogDisplayClusterProjectionSimple,    Warning, Warning);
DECLARE_LOG_CATEGORY_EXTERN(LogDisplayClusterProjectionMPCDI,     Warning, Warning);
DECLARE_LOG_CATEGORY_EXTERN(LogDisplayClusterProjectionEasyBlend, Warning, Warning);
DECLARE_LOG_CATEGORY_EXTERN(LogDisplayClusterProjectionManual,    Warning, Warning);
#else
DECLARE_LOG_CATEGORY_EXTERN(LogDisplayClusterProjection,          Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogDisplayClusterProjectionCamera,    Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogDisplayClusterProjectionSimple,    Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogDisplayClusterProjectionMPCDI,     Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogDisplayClusterProjectionEasyBlend, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogDisplayClusterProjectionManual,    Log, All);
#endif
