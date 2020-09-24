// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "FBIKConstraint.h"

struct FFBIKConstraintOption;
struct FRigElementKey;
struct FRigHierarchyContainer;

namespace FBIKConstraintLib
{
	// constraint related
	void FULLBODYIK_API ApplyConstraint(TArray<FFBIKLinkData>& InOutLinkData, TArray<ConstraintType>* Constraints);
	void FULLBODYIK_API BuildConstraints(const TArray<FFBIKConstraintOption>& Constraints, TArray<ConstraintType>& OutConstraints,
		const FRigHierarchyContainer* Hierarchy, TArray<FFBIKLinkData>& InOutLinkData, const TMap<int32, FRigElementKey>& LinkDataToHierarchyIndices,
		const TMap<FRigElementKey, int32>& HierarchyToLinkDataMap);
};

