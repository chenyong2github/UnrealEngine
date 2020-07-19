// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusNode.h"

#include "OptimusNode_GetMesh.generated.h"

class USkeletalMesh;

UCLASS()
class UOptimusNode_GetMesh
	: public UOptimusNode
{
	GENERATED_BODY()

public:
	FName GetNodeCategory() const override 
	{
		return CategoryName::Meshes;
	}

public:
	UPROPERTY(meta=(Output))
	USkeletalMesh* Mesh = nullptr;
};
