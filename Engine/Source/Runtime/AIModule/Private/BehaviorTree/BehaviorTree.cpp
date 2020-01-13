// Copyright Epic Games, Inc. All Rights Reserved.

#include "BehaviorTree/BehaviorTree.h"

DEFINE_LOG_CATEGORY(LogBehaviorTree);

UBehaviorTree::UBehaviorTree(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
}

UBlackboardData* UBehaviorTree::GetBlackboardAsset() const
{
	return BlackboardAsset;
}
