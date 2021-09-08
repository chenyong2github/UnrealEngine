// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMDefines.h"
#include "RigVMUnknownType.generated.h"

/**
 * The unknown type is used to identify untyped nodes
 */
USTRUCT()
struct RIGVM_API FRigVMUnknownType
{
	GENERATED_BODY()

	FRigVMUnknownType()
		: Hash(0)
	{}

private:
	
	UPROPERTY()
	uint32 Hash;
};
