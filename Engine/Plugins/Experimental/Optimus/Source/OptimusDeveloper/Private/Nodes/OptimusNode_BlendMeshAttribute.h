// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusNode.h"

#include "OptimusNode_BlendMeshAttribute.generated.h"

class UOptimusType_MeshAttribute;

UCLASS()
class UOptimusNode_BlendMeshAttribute
	: public UOptimusNode
{
	GENERATED_BODY()

public:
	FName GetNodeCategory() const override 
	{
		return CategoryName::Attributes;
	}

	UPROPERTY(meta = (Input))
	UOptimusType_MeshAttribute* AttributeA = nullptr;

	UPROPERTY(meta = (Input))
	UOptimusType_MeshAttribute* AttributeB = nullptr;

	UPROPERTY(EditAnywhere, Category="Blend Settings", meta = (Input))
	float Alpha = 0.5f;

	UPROPERTY(meta = (Output))
	UOptimusType_MeshAttribute* Result = nullptr;
};
