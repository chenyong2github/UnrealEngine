// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeTaskBase.h"
#include "CoreMinimal.h"

// Base class for all evaluators
UStateTreeTaskBase::UStateTreeTaskBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ID(FGuid::NewGuid())
{
}

#if WITH_EDITOR
void UStateTreeTaskBase::PostLoad()
{
	Super::PostLoad();
	if (!ID.IsValid())
	{
		ID = FGuid::NewGuid();
	}
}
#endif

#if WITH_GAMEPLAY_DEBUGGER
void UStateTreeTaskBase::AppendDebugInfoString(FString& DebugString, const FStateTreeInstance& StateTreeInstance) const
{
}
#endif

#if WITH_GAMEPLAY_DEBUGGER
void FStateTreeTask2Base::AppendDebugInfoString(FString& DebugString, const FStateTreeExecutionContext& Context) const
{
	DebugString += FString::Printf(TEXT("[%s]\n"), *Name.ToString());
}
#endif
