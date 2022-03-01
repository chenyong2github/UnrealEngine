// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_COREUOBJECT

#include "HAL/PlatformFileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/PackageResourceManager.h"


void InitCoreUObject();

void CleanupCoreUObject();

#endif // WITH_COREUOBJECT