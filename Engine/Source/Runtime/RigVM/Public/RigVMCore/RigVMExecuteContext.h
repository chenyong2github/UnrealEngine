// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMExecuteContext.generated.h"

/**
 * The execute context is used for mutable nodes to
 * indicate execution order.
 */
USTRUCT()
struct RIGVM_API FRigVMExecuteContext
{
	GENERATED_BODY()

	TArrayView<void*> OpaqueArguments;
};
