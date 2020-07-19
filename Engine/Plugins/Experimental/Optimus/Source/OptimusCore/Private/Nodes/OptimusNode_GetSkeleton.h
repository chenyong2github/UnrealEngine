// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusNode.h"

#include "OptimusNode_GetSkeleton.generated.h"

class USkeleton;
class USkeletalMesh;

UCLASS()
class UOptimusNode_GetSkeleton
	: public UOptimusNode
{
	GENERATED_BODY()

public:
	FName GetNodeCategory() const override 
	{
		return CategoryName::Attributes;
	}

	UPROPERTY(EditAnywhere, Category = "Skeletal Mesh", meta=(Input))
	USkeletalMesh* Mesh = nullptr;

	UPROPERTY(meta = (Output))
	USkeleton* Skeleton = nullptr;
};
