// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChaosFlesh/FleshCollection.h"
#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"
#include "Misc/AssertionMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogChaosFlesh, Verbose, All);

namespace ChaosFlesh 
{
	TUniquePtr<FFleshCollection> CHAOSFLESH_API ImportTetFromFile(const FString& Filename);
}
