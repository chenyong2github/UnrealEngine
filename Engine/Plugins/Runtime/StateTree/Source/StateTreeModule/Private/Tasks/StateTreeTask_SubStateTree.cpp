// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/StateTreeTask_SubStateTree.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StateTreeDelegates.h"
#include "StateTreeInstance.h"
#include "StateTreeVariableLayout.h"
#include "StateTreeConstantStorage.h"

UStateTreeTask_SubStateTree::UStateTreeTask_SubStateTree(const FObjectInitializer& ObjectInitializer)
	:Super(ObjectInitializer)
{
}


bool UStateTreeTask_SubStateTree::Initialize(FStateTreeInstance& StateTreeInstance)
{
	if (StateTree == nullptr || StateTreeInstance.GetOwner() == nullptr)
	{
		return false;
	}

	ParameterStorage.SetLayout(Parameters);

	return SubStateTreeInstance.Init(*StateTreeInstance.GetOwner(), *StateTree);
}

void UStateTreeTask_SubStateTree::Activate(FStateTreeInstance& StateTreeInstance)
{
	SubStateTreeInstance.Start();
}

void UStateTreeTask_SubStateTree::Deactivate(FStateTreeInstance& StateTreeInstance)
{
	SubStateTreeInstance.Stop();
}

EStateTreeRunStatus UStateTreeTask_SubStateTree::Tick(FStateTreeInstance& StateTreeInstance, const float DeltaTime)
{
	StateTreeInstance.WriteParametersToStorage(Parameters, ParameterStorage);
	EStateTreeRunStatus Status = SubStateTreeInstance.Tick(DeltaTime, &ParameterStorage);
	return Status;
}

#if WITH_GAMEPLAY_DEBUGGER
void UStateTreeTask_SubStateTree::AppendDebugInfoString(FString& DebugString, const FStateTreeInstance& StateTreeInstance) const
{
	Super::AppendDebugInfoString(DebugString, StateTreeInstance);
	DebugString += SubStateTreeInstance.GetDebugInfoString();
}
#endif

#if WITH_EDITOR
bool UStateTreeTask_SubStateTree::ResolveVariables(const FStateTreeVariableLayout& Variables, FStateTreeConstantStorage& Constants, UObject* Outer)
{
	return Parameters.ResolveVariables(Variables, Constants);
}

bool UStateTreeTask_SubStateTree::ValidateParameterLayout()
{
	const FStateTreeVariableLayout* ParameterLayout = StateTree ? &StateTree->GetInputParameterLayout() : nullptr;
	if (!Parameters.IsCompatible(ParameterLayout))
	{
		Modify();
		Parameters.UpdateLayout(ParameterLayout);
		return true;
	}
	return false;
}

void UStateTreeTask_SubStateTree::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property)
	{
		if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeTask_SubStateTree, StateTree))
		{
			// Update parameters from asset
			const FStateTreeVariableLayout* ParameterLayout = StateTree ? &StateTree->GetInputParameterLayout() : nullptr;
			Parameters.UpdateLayout(ParameterLayout);

			// Update UI
			UStateTree* ParentStateTree = GetTypedOuter<UStateTree>();
			if (ParentStateTree)
			{
				UE::StateTree::Delegates::OnParametersInvalidated.Broadcast(*ParentStateTree);
			}
		}
	}
}
#endif
