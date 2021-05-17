// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusNode.h"

#include "OptimusNode_GetMeshAttribute.generated.h"

class USkeletalMesh;
class UOptimusType_MeshAttribute;

UCLASS()
class UOptimusNode_GetMeshAttribute
	: public UOptimusNode
{
	GENERATED_BODY()

public:
	FName GetNodeCategory() const override 
	{
		return CategoryName::Attributes;
	}

	UPROPERTY(EditAnywhere, Category = "Mesh Attribute", meta = (Input))
	USkeletalMesh* Mesh = nullptr;

	UPROPERTY(EditAnywhere, Category = "Mesh Attribute", meta = (Input))
	FName AttributeName;

	UPROPERTY(meta = (Output))
	UOptimusType_MeshAttribute *Attribute = nullptr;
};
