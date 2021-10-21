// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeSchema.h"
#include "StateTreeTest.generated.h"

UCLASS()
class UStateTreeTestSchema : public UStateTreeSchema
{
	GENERATED_BODY()

	virtual bool IsStructAllowed(const UScriptStruct* InScriptStruct) const override { return true; }
};

