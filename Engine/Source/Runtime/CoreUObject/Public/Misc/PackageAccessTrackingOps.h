// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/PackageAccessTracking.h"
#include "UObject/NameTypes.h"

#if UE_WITH_PACKAGE_ACCESS_TRACKING

namespace PackageAccessTrackingOps
{
	extern COREUOBJECT_API FName NAME_Load;
	extern COREUOBJECT_API FName NAME_PreLoad;
	extern COREUOBJECT_API FName NAME_PostLoad;
	extern COREUOBJECT_API FName NAME_Save;
	extern COREUOBJECT_API FName NAME_CreateDefaultObject;
}

#endif //UE_WITH_PACKAGE_ACCESS_TRACKING
