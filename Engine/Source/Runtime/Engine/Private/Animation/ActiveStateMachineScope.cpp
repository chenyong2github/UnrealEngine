// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/ActiveStateMachineScope.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_StateMachine.h"

FEncounteredStateMachineStack::FEncounteredStateMachineStack(const FEncounteredStateMachineStack& ParentStack, int32 InStateMachineIndex, int32 InStateIndex) :
	StateStack(ParentStack.StateStack)
{
	StateStack.Emplace(InStateMachineIndex, InStateIndex);
}

FEncounteredStateMachineStack::FEncounteredStateMachineStack(int32 InStateMachineIndex, int32 InStateIndex)
{
	StateStack.Emplace(InStateMachineIndex, InStateIndex);
}

TSharedPtr<const FEncounteredStateMachineStack> FEncounteredStateMachineStack::InitStack(const FEncounteredStateMachineStack& ParentStack, int32 InStateMachineIndex, int32 InStateIndex)
{
	return MakeShared<const FEncounteredStateMachineStack>(ParentStack, InStateMachineIndex, InStateIndex); 
}

TSharedPtr<const FEncounteredStateMachineStack> FEncounteredStateMachineStack::InitStack(int32 InStateMachineIndex, int32 InStateIndex)
{
	return MakeShared<const FEncounteredStateMachineStack>(InStateMachineIndex, InStateIndex);
}



namespace UE { namespace Anim {

IMPLEMENT_NOTIFY_CONTEXT_INTERFACE(FAnimNotifyStateMachineContext)

FAnimNotifyStateMachineContext::FAnimNotifyStateMachineContext(const TSharedPtr<const FEncounteredStateMachineStack>& InEncounteredStateMachines)
{
	EncounteredStateMachines = InEncounteredStateMachines;
}

bool FAnimNotifyStateMachineContext::IsStateMachineInContext(int32 StateMachineIndex) const
{
	if (!EncounteredStateMachines.IsValid())
	{
		return false; 
	}

	for (const FEncounteredStateMachineStack::FStateMachineEntry& Entry : EncounteredStateMachines->StateStack)
	{
		if (Entry.StateMachineIndex == StateMachineIndex)
		{
			return true; 
		}
	}
	return false; 
}

bool FAnimNotifyStateMachineContext::IsStateInStateMachineInContext(int32 StateMachineIndex, int32 StateIndex) const
{
	if (!EncounteredStateMachines.IsValid())
	{
		return false;
	}

	for (const FEncounteredStateMachineStack::FStateMachineEntry& Entry : EncounteredStateMachines->StateStack)
	{
		if (Entry.StateMachineIndex == StateMachineIndex && Entry.StateIndex == StateIndex)
		{
			return true;
		}
	}
	return false;
}

IMPLEMENT_ANIMGRAPH_MESSAGE(FActiveStateMachineScope);

FActiveStateMachineScope::FActiveStateMachineScope(const FAnimationBaseContext& InContext, FAnimNode_StateMachine* StateMachine, int32 InStateIndex)
{
	int32 StateMachineIndex = GetStateMachineIndex(StateMachine, InContext);
	if (FActiveStateMachineScope* ParentStateMachineScope = InContext.GetMessage<FActiveStateMachineScope>())
	{
		ActiveStateMachines = FEncounteredStateMachineStack::InitStack(*(ParentStateMachineScope->ActiveStateMachines.Get()), StateMachineIndex, InStateIndex); 
	}
	else
	{
		ActiveStateMachines = FEncounteredStateMachineStack::InitStack(StateMachineIndex, InStateIndex);
	}
}

FActiveStateMachineScope::~FActiveStateMachineScope()
{
}

int32 FActiveStateMachineScope::GetStateMachineIndex(FAnimNode_StateMachine* StateMachine, const FAnimationBaseContext& Context)
{
	if (Context.AnimInstanceProxy)
	{
		return Context.AnimInstanceProxy->GetStateMachineIndex(StateMachine);
	}
	return INDEX_NONE;
}

TSharedPtr<const IAnimNotifyEventContextDataInterface> FActiveStateMachineScope::MakeEventContextData() const
{
	return MakeShared<const FAnimNotifyStateMachineContext>(ActiveStateMachines);
}
	
}}	// namespace UE::Anim