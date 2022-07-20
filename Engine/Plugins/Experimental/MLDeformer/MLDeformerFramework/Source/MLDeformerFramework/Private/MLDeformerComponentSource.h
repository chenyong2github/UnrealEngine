// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusComponentSource.h"

#include "MLDeformerComponentSource.generated.h"


UCLASS(meta=(DisplayName="ML Deformer Component"))
class UMLDeformerComponentSource :
	public UOptimusComponentSource
{
	GENERATED_BODY()
public:
	struct Contexts
	{
		static FName Vertex;
	};
	
	// UOptimusComponentSource implementations
	FText GetDisplayName() const override;
	FName GetBindingName() const override { return FName("MLDeformer"); }
	TSubclassOf<UActorComponent> GetComponentClass() const override;
	TArray<FName> GetExecutionContexts() const override;
};
