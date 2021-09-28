// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTypes.h"
#include "StateTreeParameter.h"
#include "StateTreeParameterLayout.generated.h"

struct FStateTreeVariableLayout;
struct FStateTreeConstantStorage;

/**
 * Defines layout of parameters.
 * Can be used in the UI to define a how StateTree variables or constants are bound to specific parameters, see StateTreeParameterLayoutDetails.
 */
USTRUCT()
struct FStateTreeParameterLayout
{
	GENERATED_BODY()

public:
	void Reset() { Parameters.Reset(); }

#if WITH_EDITOR
	bool ResolveVariables(const FStateTreeVariableLayout& Variables, FStateTreeConstantStorage& Constants);
	bool IsCompatible(const FStateTreeVariableLayout* Layout) const;
	void UpdateLayout(const FStateTreeVariableLayout* NewLayout);
#endif

	UPROPERTY(EditDefaultsOnly, Category = Parameters, EditFixedSize, meta = (EditFixedOrder));
	TArray<FStateTreeParameter> Parameters;
};
