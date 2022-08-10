// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#if WITH_EDITOR

#include "CoreMinimal.h"

class UPCGSettings;

namespace PCGDeterminismTests
{
	PCG_API TFunction<bool()> GetNativeTestIfExists(const UPCGSettings* PCGSettings);
}

#endif