// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldConditionBase.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(WorldConditionBase)

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
