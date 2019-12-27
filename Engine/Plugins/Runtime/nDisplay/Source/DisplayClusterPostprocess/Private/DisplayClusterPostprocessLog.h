// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if UE_BUILD_SHIPPING
DECLARE_LOG_CATEGORY_EXTERN(LogDisplayClusterPostprocess,            Warning, Warning);
DECLARE_LOG_CATEGORY_EXTERN(LogDisplayClusterPostprocessOutputRemap, Warning, Warning);
#else
DECLARE_LOG_CATEGORY_EXTERN(LogDisplayClusterPostprocess,            Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogDisplayClusterPostprocessOutputRemap, Log, All);
#endif
