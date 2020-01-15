// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if UE_BUILD_SHIPPING
DECLARE_LOG_CATEGORY_EXTERN(LogPicpProjection,          Warning, Warning);
DECLARE_LOG_CATEGORY_EXTERN(LogPicpProjectionMPCDI,     Warning, Warning);
#else
DECLARE_LOG_CATEGORY_EXTERN(LogPicpProjection,          Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogPicpProjectionMPCDI,     Log, All);
#endif
