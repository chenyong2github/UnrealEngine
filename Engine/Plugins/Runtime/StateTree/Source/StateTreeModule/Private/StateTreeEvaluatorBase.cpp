// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeEvaluatorBase.h"
#include "CoreMinimal.h"
#include "StateTree.h"
#include "StateTreeDelegates.h"

// Base class for all evaluators
UStateTreeEvaluatorBase::UStateTreeEvaluatorBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	ID = FGuid::NewGuid();
#endif // WITH_EDITORONLY_DATA
}

#if WITH_GAMEPLAY_DEBUGGER
void UStateTreeEvaluatorBase::AppendDebugInfoString(FString& DebugString, const FStateTreeInstance& StateTreeInstance) const
{
	DebugString += FString::Printf(TEXT("[%s]\n"), *Name.ToString());
}
#endif // WITH_GAMEPLAY_DEBUGGER

#if WITH_EDITOR
void UStateTreeEvaluatorBase::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property)
	{
		if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeEvaluatorBase, Name))
		{
			UStateTree* StateTree = GetTypedOuter<UStateTree>();
			if (ensure(StateTree))
			{
				UE::StateTree::Delegates::OnIdentifierChanged.Broadcast(*StateTree);
			}
		}
	}
}
#endif

#if WITH_GAMEPLAY_DEBUGGER
void FStateTreeEvaluator2Base::AppendDebugInfoString(FString& DebugString, const FStateTreeExecutionContext& Context) const
{
	DebugString += FString::Printf(TEXT("[%s]\n"), *Name.ToString());
}
#endif // WITH_GAMEPLAY_DEBUGGER
