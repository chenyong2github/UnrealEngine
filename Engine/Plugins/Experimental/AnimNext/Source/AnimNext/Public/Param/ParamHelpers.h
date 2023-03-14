// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FAnimNextParamType;

namespace UE::AnimNext
{

struct FParamTypeHandle;

struct ANIMNEXT_API FParamHelpers
{
	enum class ECopyResult
	{
		Failed,
		Succeeded,
	};

	/** Copy a parameter from one type to another, validating type, container and any polymorphism rules if the types differ at all. */
	static ECopyResult Copy(const FAnimNextParamType& InSourceType, const FAnimNextParamType& InTargetType, TConstArrayView<uint8> InSourceMemory, TArrayView<uint8> InTargetMemory);

	/** Copy a parameter from one type to another, validating type, container and any polymorphism rules if the types differ at all. */
	static ECopyResult Copy(const FParamTypeHandle& InSourceTypeHandle, const FParamTypeHandle& InTargetTypeHandle, TConstArrayView<uint8> InSourceMemory, TArrayView<uint8> InTargetMemory);
};

}