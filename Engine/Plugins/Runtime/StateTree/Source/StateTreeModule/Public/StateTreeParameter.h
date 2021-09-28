// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTypes.h"
#include "Misc/Guid.h"
#include "StateTreeVariable.h"
#include "StateTreeParameter.generated.h"

struct FStateTreeVariableDesc;

/**
 * Describes a named reference to a variable in the StateTree.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeParameter
{
	GENERATED_BODY()

#if WITH_EDITOR
	bool IsCompatible(const FStateTreeVariableDesc& Desc) const;
#endif

	UPROPERTY(EditDefaultsOnly, Category = Parameters);
	FName Name;

	UPROPERTY(EditDefaultsOnly, Category = Parameters);
	FStateTreeVariable Variable;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditDefaultsOnly, Category = Value)
	FGuid ID;
#endif
};
