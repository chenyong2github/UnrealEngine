// Copyright Epic Games, Inc. All Rights Reserved.

#include "Debugger/StateTreeTraceTypes.h"

#include "StateTree.h"
#include "StateTreeNodeBase.h"

#if WITH_STATETREE_DEBUGGER

//----------------------------------------------------------------------//
// FStateTreeTraceLogEvent
//----------------------------------------------------------------------//
FString FStateTreeTraceLogEvent::ToString(const UStateTree& StateTree) const
{
	return (*Message);
}


//----------------------------------------------------------------------//
// FStateTreeTraceTransitionEvent
//----------------------------------------------------------------------//
FString FStateTreeTraceTransitionEvent::ToString(const UStateTree& StateTree) const
{
	if (const FCompactStateTransition* Transition = StateTree.GetTransitionFromIndex(TransitionIndex.Get()))
	{
		const FCompactStateTreeState* CompactState = StateTree.GetStateFromHandle(Transition->State);
		FStringBuilderBase StrBuilder;
		StrBuilder.Appendf(TEXT("%s Goto %s (Priority: %s)"),
					*UEnum::GetDisplayValueAsText(Transition->Trigger).ToString(),
					CompactState != nullptr ? *CompactState->Name.ToString() : *Transition->State.Describe(),
					*UEnum::GetDisplayValueAsText(Transition->Priority).ToString());

		if (Transition->EventTag.IsValid())
		{
			StrBuilder.Appendf(TEXT("\n\t%s"), *Transition->EventTag.ToString()); 
		}

		return StrBuilder.ToString();
	}

	return FString::Printf(TEXT("Invalid Transition Index %s for '%s'"), *LexToString(TransitionIndex.Get()), *StateTree.GetFullName()); 
}


//----------------------------------------------------------------------//
// FStateTreeTraceNodeEvent
//----------------------------------------------------------------------//
FString FStateTreeTraceNodeEvent::ToString(const UStateTree& StateTree) const
{
	const FConstStructView NodeView = StateTree.GetNode(Index.Get());
	const FStateTreeNodeBase* Node = NodeView.GetPtr<const FStateTreeNodeBase>();

	return FString::Printf(TEXT("%s '%s (%s)'"),
			*UEnum::GetDisplayValueAsText(EventType).ToString(),
			Node != nullptr ? *Node->Name.ToString() : *LexToString(Index.Get()),
			*NodeView.GetScriptStruct()->GetName());
}


//----------------------------------------------------------------------//
// FStateTreeTraceStateEvent
//----------------------------------------------------------------------//
FStateTreeStateHandle FStateTreeTraceStateEvent::GetStateHandle() const
{
	return FStateTreeStateHandle(Index.Get());
}

FString FStateTreeTraceStateEvent::ToString(const UStateTree& StateTree) const
{
	const FStateTreeStateHandle StateHandle(Index.Get());
	if (const FCompactStateTreeState* CompactState = StateTree.GetStateFromHandle(StateHandle))
	{
		if (SelectionBehavior != EStateTreeStateSelectionBehavior::None)
		{
			return FString::Printf(TEXT("%s '%s' (%s)"),
					*UEnum::GetDisplayValueAsText(EventType).ToString(),
					*CompactState->Name.ToString(),
					*UEnum::GetDisplayValueAsText(SelectionBehavior).ToString());
		}
		else
		{
			return FString::Printf(TEXT("%s '%s'"),
					*UEnum::GetDisplayValueAsText(EventType).ToString(),
					*CompactState->Name.ToString());
		}
	}

	return FString::Printf(TEXT("Invalid State Index %s for '%s'"), *StateHandle.Describe(), *StateTree.GetFullName());
}


//----------------------------------------------------------------------//
// FStateTreeTraceTaskEvent
//----------------------------------------------------------------------//
FString FStateTreeTraceTaskEvent::ToString(const UStateTree& StateTree) const
{
	return FString::Printf(TEXT("<%s> %s"),	*UEnum::GetDisplayValueAsText(Status).ToString(), *FStateTreeTraceNodeEvent::ToString(StateTree));
}


//----------------------------------------------------------------------//
// FStateTreeTraceConditionEvent
//----------------------------------------------------------------------//
FString FStateTreeTraceConditionEvent::ToString(const UStateTree& StateTree) const
{
	return FStateTreeTraceNodeEvent::ToString(StateTree);
}


//----------------------------------------------------------------------//
// FStateTreeTraceActiveStatesEvent
//----------------------------------------------------------------------//
FStateTreeTraceActiveStatesEvent::FStateTreeTraceActiveStatesEvent(const double RecordingWorldTime, const EStateTreeUpdatePhase Phase)
	: FStateTreeTracePhaseEvent(RecordingWorldTime, Phase)
{
}

FString FStateTreeTraceActiveStatesEvent::ToString(const UStateTree& StateTree) const
{
	FStringBuilderBase StatePath;
	for (int32 i = 0; i < ActiveStates.Num(); i++)
	{
		const FCompactStateTreeState& State = StateTree.GetStates()[ActiveStates[i].Index];
		StatePath.Appendf(TEXT("%s%s"), i == 0 ? TEXT("") : TEXT("."), *State.Name.ToString());
	}

	return FString::Printf(TEXT("New active states: '%s'"), *StatePath);
}

#endif // WITH_STATETREE_DEBUGGER
