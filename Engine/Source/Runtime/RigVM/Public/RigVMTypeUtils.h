// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace RigVMTypeUtils
{
	const TCHAR TArrayPrefix[] = TEXT("TArray<");
	const TCHAR TObjectPtrPrefix[] = TEXT("TObjectPtr<");
	const TCHAR TArrayTemplate[] = TEXT("TArray<%s>");
	const TCHAR TObjectPtrTemplate[] = TEXT("TObjectPtr<%s%s>");
	
	// Returns true if the type specified is an array
	FORCEINLINE bool IsArrayType(const FString& InCPPType)
	{
		return InCPPType.StartsWith(TArrayPrefix);
	}

	FORCEINLINE FString ArrayTypeFromBaseType(const FString& InCPPType)
	{
		return FString::Printf(TArrayTemplate, *InCPPType);
	}

	FORCEINLINE FString BaseTypeFromArrayType(const FString& InCPPType)
	{
		return InCPPType.RightChop(7).LeftChop(1);
	}

	FORCEINLINE bool IsUObjectType(const FString& InCPPType)
	{
		return InCPPType.StartsWith(TObjectPtrPrefix);
	}
}