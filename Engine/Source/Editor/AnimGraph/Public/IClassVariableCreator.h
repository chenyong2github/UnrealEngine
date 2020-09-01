// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "IClassVariableCreator.generated.h"

class FKismetCompilerContext;

UINTERFACE(MinimalAPI)
class UClassVariableCreator : public UInterface
{
	GENERATED_BODY()
};

class IClassVariableCreator
{
	GENERATED_BODY()

public:
	/** 
	 * Implement this in a graph node and the anim BP compiler will call this expecting to generate
	 * class variables.
	 * @param	InCompilerContext	The compiler context for the current BP compilation
	 */
	virtual void CreateClassVariablesFromBlueprint(FKismetCompilerContext& InCompilerContext) = 0;
};