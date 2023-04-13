// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsManagerComponent.h"
#include "LearningAgentsManager.h"
#include "LearningLog.h"

ULearningAgentsManagerComponent::ULearningAgentsManagerComponent() : UActorComponent() {}
ULearningAgentsManagerComponent::ULearningAgentsManagerComponent(FVTableHelper& Helper) : ULearningAgentsManagerComponent() {}
ULearningAgentsManagerComponent::~ULearningAgentsManagerComponent() {}

bool ULearningAgentsManagerComponent::AddAgent(const int32 AgentId)
{
	if (HasAgent(AgentId))
	{
		UE_LOG(LogLearning, Warning, TEXT("AgentId %i is already added to %s."), AgentId, *GetName());
		return false;
	}

	AddedAgentIds.Add(AgentId);
	AddedAgentSet = AddedAgentIds;
	AddedAgentSet.TryMakeSlice();

	return true;
}

bool ULearningAgentsManagerComponent::RemoveAgent(const int32 AgentId)
{
	if (AddedAgentIds.RemoveSingleSwap(AgentId, false) == 0)
	{
		UE_LOG(LogLearning, Warning, TEXT("Unable to remove: AgentId %d not found in %s's agent set."), AgentId, *GetName());
		return false;
	}

	AddedAgentSet = AddedAgentIds;
	AddedAgentSet.TryMakeSlice();

	return true;
}

bool ULearningAgentsManagerComponent::HasAgent(const int32 AgentId) const
{
	return AddedAgentSet.Contains(AgentId);
}

UObject* ULearningAgentsManagerComponent::GetAgent(const int32 AgentId, const TSubclassOf<UObject> AgentClass) const
{
	if (!AgentManager)
	{
		UE_LOG(LogLearning, Error, TEXT("Agent manager is nullptr. Call setup on this component prior to getting agents."));
		return nullptr;
	}

	if (!AddedAgentSet.Contains(AgentId))
	{
		UE_LOG(LogLearning, Error,
			TEXT("%s: AgentId %d not found. Be sure to only use AgentIds returned by AddAgent() and check that the agent has not be removed."),
			*GetName(),
			AgentId);
		return nullptr;
	}

	return AgentManager->GetAgent(AgentId, AgentClass); // Calling this overload since it will log about missing manager ids
}

const UObject* ULearningAgentsManagerComponent::GetAgent(const int32 AgentId) const
{
	UE_LEARNING_CHECKF(AgentManager, TEXT("AgentManager is nullptr. Did we forget to call Setup on this component and set the manager?"));
	UE_LEARNING_CHECKF(AddedAgentSet.Contains(AgentId), TEXT("AgentId not found. Make sure it was added via AddAgent()."));

	return AgentManager->GetAgent(AgentId);
}

UObject* ULearningAgentsManagerComponent::GetAgent(const int32 AgentId)
{
	UE_LEARNING_CHECKF(AgentManager, TEXT("AgentManager is nullptr. Did we forget to call Setup on this component and set the manager?"));
	UE_LEARNING_CHECKF(AddedAgentSet.Contains(AgentId), TEXT("AgentId not found. Make sure it was added via AddAgent()."));

	return AgentManager->GetAgent(AgentId);
}

ALearningAgentsManager* ULearningAgentsManagerComponent::GetAgentManager() const
{
	UE_LEARNING_CHECKF(AgentManager, TEXT("AgentManager is nullptr. Did we forget to call Setup on this component and set the manager?"));

	return AgentManager;
}

ALearningAgentsManager* ULearningAgentsManagerComponent::GetAgentManager(const TSubclassOf<ALearningAgentsManager> AgentManagerClass) const
{
	if (!AgentManager)
	{
		UE_LOG(LogLearning, Error, TEXT("AgentManager is nullptr. Did we forget to call Setup on this component and set the manager?"));
		return nullptr;
	}

	return AgentManager;
}

bool ULearningAgentsManagerComponent::IsSetup() const
{
	return bIsSetup;
}
