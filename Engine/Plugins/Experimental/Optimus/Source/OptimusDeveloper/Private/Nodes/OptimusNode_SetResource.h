// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusNode_ResourceAccessorBase.h"

#include "OptimusNode_SetResource.generated.h"


UCLASS(Hidden)
class UOptimusNode_SetResource
	: public UOptimusNode_ResourceAccessorBase
{
	GENERATED_BODY()

protected:
	void ConstructNode() override;
};
