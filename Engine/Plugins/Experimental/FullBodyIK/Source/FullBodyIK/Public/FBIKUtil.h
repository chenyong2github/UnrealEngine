// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FRigHierarchyContainer;
struct FRigElementKey;

namespace FBIKUtil
{
	/** Utility functions */
	FVector FULLBODYIK_API GetScaledRotationAxis(const FQuat& InQuat);
	bool FULLBODYIK_API CanCrossProduct(const FVector& Vector1, const FVector& Vector2);
	bool FULLBODYIK_API GetBoneChain(FRigHierarchyContainer* Hierarchy, const FRigElementKey& Root, const FRigElementKey& Current, TArray<FRigElementKey>& ChainIndices);
};


