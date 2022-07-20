// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusComponentSource.h"

#include "OptimusSkeletalMeshComponentSource.generated.h"


UCLASS()
class UOptimusSkeletalMeshComponentSource :
	public UOptimusComponentSource
{
	GENERATED_BODY()
public:
	struct Contexts
	{
		static FName Vertex;
		static FName Triangle;
		static FName Bone;
	};
	
	// UOptimusComponentSource implementations
	FText GetDisplayName() const override;
	FName GetBindingName() const override { return FName("SkeletalMesh"); }
	TSubclassOf<UActorComponent> GetComponentClass() const override;
	TArray<FName> GetExecutionContexts() const override;
};
