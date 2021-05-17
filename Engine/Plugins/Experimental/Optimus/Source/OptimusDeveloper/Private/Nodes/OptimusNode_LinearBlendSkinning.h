// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusNode.h"

#include "OptimusNode_LinearBlendSkinning.generated.h"

class UOptimusType_MeshSkinWeights;
class UOptimusType_MeshAttribute;
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
	UOptimusType_MeshSkinWeights* SkinWeights = nullptr;

	UPROPERTY(meta = (Input))
	UOptimusType_MeshAttribute* EffectWeights = nullptr;

	UPROPERTY(meta = (Output))
	USkeletalMesh* Result = nullptr;
};
