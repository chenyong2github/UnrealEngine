// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusNode.h"

#include "OptimusNode_LinearBlendSkinning.generated.h"

class UOptimusMeshSkinWeights;
class UOptimusMeshAttribute;
class USkeleton;
class USkeletalMesh;

UCLASS()
class UOptimusNode_LinearBlendSkinning
	: public UOptimusNode
{
	GENERATED_BODY()

public:
	FName GetNodeCategory() const override { return CategoryName::Deformers; }

	UPROPERTY(meta = (Input))
	USkeletalMesh* Mesh = nullptr;

	UPROPERTY(meta = (Input))
	USkeleton* Skeleton = nullptr;

	UPROPERTY(meta = (Input))
	UOptimusMeshSkinWeights* SkinWeights = nullptr;

	UPROPERTY(meta = (Input))
	UOptimusMeshAttribute* EffectWeights = nullptr;

	UPROPERTY(meta = (Output))
	USkeletalMesh* Result = nullptr;
};
