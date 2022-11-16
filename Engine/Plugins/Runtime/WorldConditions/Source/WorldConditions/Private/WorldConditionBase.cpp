// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldConditionBase.h"
#include "WorldConditionQuery.h"

FWorldConditionBase::~FWorldConditionBase()
{
	// Empty
}

FText FWorldConditionBase::GetDescription() const
{
	return StaticStruct()->GetDisplayNameText();
}

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
