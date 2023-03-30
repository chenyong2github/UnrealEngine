// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsTypeComponent.h"

#include "Engine/World.h"
#include "LearningAgentsType.h"
#include "LearningLog.h"
#include "UObject/NameTypes.h"

ULearningAgentsTypeComponent::ULearningAgentsTypeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}
ULearningAgentsTypeComponent::ULearningAgentsTypeComponent(FVTableHelper& Helper) : ULearningAgentsTypeComponent() {}
ULearningAgentsTypeComponent::~ULearningAgentsTypeComponent() {}

void ULearningAgentsTypeComponent::OnRegister()
{
	Super::OnRegister();

	if (!GetWorld()->IsGameWorld())
	{
		// We're not in a game yet so we don't need to register all the callbacks
		return;
	}

	USceneComponent* Parent = GetAttachParent();
	if (Parent)
	{
		AgentType = Cast<ULearningAgentsType>(Parent);
		if (AgentType)
		{
			AgentType->GetOnSetupComplete().AddUObject(this, &ULearningAgentsTypeComponent::OnAgentTypeSetupComplete);
			AgentType->GetOnAgentAdded().AddUObject(this, &ULearningAgentsTypeComponent::OnAgentAdded);
			AgentType->GetOnAgentRemoved().AddUObject(this, &ULearningAgentsTypeComponent::OnAgentRemoved);
			return;
		}
	}

	UE_LOG(LogLearning, Warning,
		TEXT("%s: Not attached to ULearningAgentType. OnAgentTypeSetupComplete/OnAgentRemoved will not be called. " 
			"If you wish to use these events, make sure to attach this to an ULearningAgentType component."),
		*GetName());
}

void ULearningAgentsTypeComponent::OnAgentTypeSetupComplete_Implementation()
{
	// Can be overridden in child class
}

void ULearningAgentsTypeComponent::OnAgentAdded_Implementation(int32 AgentId, UObject* Agent)
{
	AddAgent(AgentId);
}

void ULearningAgentsTypeComponent::AddAgent(int32 AgentId)
{
	if (AgentId < 0)
	{
		UE_LOG(LogLearning, Error, TEXT("Unable to add: AgentId must be a valid index. AgentId was %d"), AgentId);
		return;
	}

	const UE::Learning::FIndexSet ExistingAgents = AgentType->GetOccupiedAgentSet();
	if (!ExistingAgents.Contains(AgentId))
	{
		UE_LOG(LogLearning, Error,
			TEXT("Unable to add: AgentId %d not found on AgentType. Make sure to add agents to the agent type before adding."),
			AgentId);
		return;
	}

	SelectedAgentIds.Add(AgentId);
	UpdateAgentSet();
}

void ULearningAgentsTypeComponent::OnAgentRemoved_Implementation(int32 AgentId, UObject* Agent)
{
	RemoveAgent(AgentId);
}

void ULearningAgentsTypeComponent::RemoveAgent(int32 AgentId)
{
	if (AgentId < 0)
	{
		UE_LOG(LogLearning, Warning, TEXT("Unable to remove: AgentId must be a valid index. AgentId was %d"), AgentId);
		return;
	}

	int32 RemovedCount = SelectedAgentIds.RemoveSingleSwap(AgentId, false);
	if (RemovedCount == 0)
	{
		UE_LOG(LogLearning, Warning, TEXT("Unable to remove: AgentId %d not found in the added agents set."), AgentId);
		return;
	}

	UpdateAgentSet();
}

const ULearningAgentsType& ULearningAgentsTypeComponent::GetAgentType()
{
	return *AgentType;
}

UObject* ULearningAgentsTypeComponent::GetAgent(int32 AgentId, TSubclassOf<UObject> AgentClass)
{
	return AgentType->GetAgent(AgentId, AgentClass);
}

const UObject* ULearningAgentsTypeComponent::GetAgent(int32 AgentId) const
{
	return AgentType->GetAgent(AgentId);
}

void ULearningAgentsTypeComponent::UpdateAgentSet()
{
	SelectedAgentsSet = SelectedAgentIds;
	SelectedAgentsSet.TryMakeSlice();
}
