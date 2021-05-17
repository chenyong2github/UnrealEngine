// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusNode.h"

#include "OptimusNode_NormalizeMeshTangents.generated.h"

class USkeletalMesh;

UCLASS()
class UOptimusNode_NormalizeMeshTangents
	: public UOptimusNode
{
	GENERATED_BODY()

public:
	FName GetNodeCategory() const override { return CategoryName::Deformers; }

	UPROPERTY(meta = (Input))
	USkeletalMesh* Mesh = nullptr;

	UPROPERTY(meta = (Output))
	USkeletalMesh* Result = nullptr;
};
