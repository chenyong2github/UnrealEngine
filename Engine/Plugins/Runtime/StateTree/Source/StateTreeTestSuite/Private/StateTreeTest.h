// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeSchema.h"

#include "StateTreeTest.generated.h"

UCLASS()
class UStateTreeTestSchema : public UStateTreeSchema
{
	GENERATED_BODY()

public:
	/** @return True if we should use StateTree V2 */
	virtual bool IsV2() const { return true; }
};

