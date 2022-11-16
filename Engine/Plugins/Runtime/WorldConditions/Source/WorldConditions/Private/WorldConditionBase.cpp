// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldConditionBase.h"
#include "WorldConditionQuery.h"
#include "WorldConditionSchema.h"

FWorldConditionBase::~FWorldConditionBase()
{
	// Empty
}

#if WITH_EDITOR
FText FWorldConditionBase::GetDescription() const
{
	return StaticStruct()->GetDisplayNameText();
}
#endif

bool FWorldConditionBase::Initialize(const UWorldConditionSchema& Schema)
{
	return true;
}

bool FWorldConditionBase::Activate(const FWorldConditionContext& Context) const
{
	return true;
}

EWorldConditionResult FWorldConditionBase::IsTrue(const FWorldConditionContext& Context) const
{
	return EWorldConditionResult::IsTrue;
}

void FWorldConditionBase::Deactivate(const FWorldConditionContext& Context) const
{
	// Empty
}

void FWorldConditionBase::InvalidateResult(FWorldConditionQueryState& QueryState) const
{
	QueryState.CachedResult = EWorldConditionResult::Invalid;
	QueryState.GetItem(ConditionIndex).CachedResult = EWorldConditionResult::Invalid; 
}
