// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

#include "OptimusNodeLink.generated.h"

class UOptimusNodePin;

UCLASS()
class OPTIMUSCORE_API UOptimusNodeLink : public UObject
{
	GENERATED_BODY()

public:
	UOptimusNodeLink() = default;

	UOptimusNodePin* GetNodeOutputPin() const { return NodeOutputPin; }
	UOptimusNodePin* GetNodeInputPin() const { return NodeInputPin; }

protected:
	friend class UOptimusNodeGraph;

	UPROPERTY()
	UOptimusNodePin* NodeOutputPin;

	UPROPERTY()
	UOptimusNodePin* NodeInputPin;
};
