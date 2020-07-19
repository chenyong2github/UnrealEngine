// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusNode.h"

#include "OptimusNode_GetMeshSkinWeights.generated.h"

class USkeletalMesh;
class UOptimusMeshSkinWeights;

UCLASS()
class UOptimusNode_GetMeshSkinWeights
	: public UOptimusNode
{
	GENERATED_BODY()

public:
	FName GetNodeCategory() const override 
	{
		return CategoryName::Attributes;
	}

	UPROPERTY(EditAnywhere, Category = "Skin Profile", meta = (Input))
	USkeletalMesh* Mesh = nullptr;

	UPROPERTY(EditAnywhere, Category = "Skin Profile", meta = (Input))
	FName SkinWeightProfileName;

	UPROPERTY(meta = (Output))
	UOptimusMeshSkinWeights *SkinWeights = nullptr;
};
