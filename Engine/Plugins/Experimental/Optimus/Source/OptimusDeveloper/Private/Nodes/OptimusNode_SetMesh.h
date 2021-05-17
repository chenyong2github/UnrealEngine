// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusNode.h"

#include "OptimusNode_SetMesh.generated.h"

class USkeletalMesh;

UCLASS()
class UOptimusNode_SetMesh
	: public UOptimusNode
{
	GENERATED_BODY()

public:
	FName GetNodeCategory() const override 
	{ 
		return CategoryName::Meshes; 
	}

	UPROPERTY(meta = (Input))
	USkeletalMesh* Mesh = nullptr;
};
