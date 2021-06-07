// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace RigVMUtilities
{
	// Returns true if the type specified is an array
	FORCEINLINE bool IsArrayType(const FString& InCPPType)
	{
		return InCPPType.StartsWith(TEXT("TArray<"));
	}
}