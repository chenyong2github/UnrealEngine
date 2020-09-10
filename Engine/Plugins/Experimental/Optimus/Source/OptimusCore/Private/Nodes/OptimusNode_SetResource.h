// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusNode_ResourceAccessorBase.h"

#include "OptimusNode_SetResource.generated.h"


UCLASS()
class UOptimusNode_SetResource
	: public UOptimusNode_ResourceAccessorBase
{
	GENERATED_BODY()

protected:
	void CreatePins() override;
};
