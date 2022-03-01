// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/QueuedThreadPool.h"
#include "UObject/PackageResourceManager.h"

#if WITH_EDITORONLY_DATA && defined(DERIVEDDATACACHE_API)
#include "DerivedDataBuild.h"
#include "DerivedDataCache.h"
#endif // WITH_EDITORONLY_DATA && defined(DERIVEDDATACACHE_API)
