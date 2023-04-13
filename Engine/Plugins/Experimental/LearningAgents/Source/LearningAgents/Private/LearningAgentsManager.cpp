// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsManager.h"
#include "LearningAgentsManagerComponent.h"

#include "LearningArray.h"
#include "LearningArrayMap.h"
#include "LearningLog.h"

#include "UObject/ScriptInterface.h"
#include "EngineDefines.h"

ALearningAgentsManager::ALearningAgentsManager() : AActor() {}
ALearningAgentsManager::ALearningAgentsManager(FVTableHelper& Helper) : ALearningAgentsManager() {}
ALearningAgentsManager::~ALearningAgentsManager() {}

void ALearningAgentsManager::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	// Pre-populate the vacant ids
	OccupiedAgentIds.Reserve(MaxInstanceNum);
	VacantAgentIds.Reserve(MaxInstanceNum);
	for (int32 i = MaxInstanceNum - 1; i >= 0; i--)
	{
		VacantAgentIds.Push(i);
		Agents.Add(nullptr);
	}

	UpdateAgentSets();
}

void ALearningAgentsManager::SetupManager()
{
	if (IsManagerSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup already run!"), *GetName());
		return;
	}

	InstanceData = MakeShared<UE::Learning::FArrayMap>();

	GetComponents<ULearningAgentsManagerComponent>(CachedManagerComponents);
	if (CachedManagerComponents.IsEmpty())
	{
		UE_LOG(LogLearning, Warning, TEXT("%s: Found zero attached manager components during setup."), *GetName());
	}
	else
	{
		for (const ULearningAgentsManagerComponent* ManagerComponent : CachedManagerComponents)
		{
			UE_LOG(LogLearning, Display, TEXT("Added %s manager component to %s's cache."), *ManagerComponent->GetName(), *GetName());
		}
	}

	bIsSetup = true;
}

bool ALearningAgentsManager::IsManagerSetup() const
{
	return bIsSetup;
}

int32 ALearningAgentsManager::GetMaxInstanceNum() const
{
	return MaxInstanceNum;
}

const TSharedPtr<UE::Learning::FArrayMap>& ALearningAgentsManager::GetInstanceData() const
{
	return InstanceData;
}

TConstArrayView<TObjectPtr<UObject>> ALearningAgentsManager::GetAgents() const
{
	return Agents;
}

int32 ALearningAgentsManager::AddAgent(UObject* Agent)
{
	if (Agent == nullptr)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Attempted to add an agent but agent is nullptr."), *GetName());
		return INDEX_NONE;
	}

	if (VacantAgentIds.IsEmpty())
	{
		UE_LOG(LogLearning, Error,
			TEXT("%s: Attempting to add an agent but we have no more vacant ids. Increase MaxInstanceNum (%d) or remove unused agents."),
			*GetName(),
			MaxInstanceNum);
		return INDEX_NONE;
	}

	// Add Agent
	const int32 NewAgentId = VacantAgentIds.Pop();
	Agents[NewAgentId] = Agent;

	OccupiedAgentIds.Add(NewAgentId);

	UpdateAgentSets();

	OnAgentAdded(NewAgentId);

	return NewAgentId;
}

void ALearningAgentsManager::OnAgentAdded_Implementation(const int32 AgentId)
{
	for (ULearningAgentsManagerComponent* ManagerComponent : CachedManagerComponents)
	{
		ManagerComponent->AddAgent(AgentId);
	}
}

void ALearningAgentsManager::RemoveAgentById(const int32 AgentId)
{
	if (AgentId == INDEX_NONE)
	{
		UE_LOG(LogLearning, Warning, TEXT("%s: Attempting to remove an agent with id of INDEX_NONE."), *GetName());
		return;
	}

	const int32 RemovedCount = OccupiedAgentIds.RemoveSingleSwap(AgentId, false);

	if (RemovedCount == 0)
	{
		UE_LOG(LogLearning, Warning, TEXT("%s: Trying to remove an agent with id of %i but it was not found."), *GetName(), AgentId);
		return;
	}

	// Remove Agent
	VacantAgentIds.Push(AgentId);
	Agents[AgentId] = nullptr;

	UpdateAgentSets();

	OnAgentRemoved(AgentId);
}

void ALearningAgentsManager::RemoveAgent(UObject* Agent)
{
	if (Agent == nullptr)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Attempted to remove agent but agent is nullptr."), *GetName());
		return;
	}

	int32 AgentId = INDEX_NONE;
	const bool bIsFound = Agents.Find(Agent, AgentId);

	if (!bIsFound)
	{
		UE_LOG(LogLearning, Warning, TEXT("%s: Trying to remove agent %s but it was not found."), *Agent->GetName(), *GetName());
		return;
	}

	RemoveAgentById(AgentId);
}

void ALearningAgentsManager::OnAgentRemoved_Implementation(const int32 AgentId)
{
	for (ULearningAgentsManagerComponent* ManagerComponent : CachedManagerComponents)
	{
		if (ManagerComponent->HasAgent(AgentId))
		{
			ManagerComponent->RemoveAgent(AgentId);
		}
	}
}

bool ALearningAgentsManager::HasAgent(UObject* Agent) const
{
	return Agents.Find(Agent) ? true : false;
}

bool ALearningAgentsManager::HasAgentById(const int32 AgentId) const
{
	return OccupiedAgentSet.Contains(AgentId);
}

UObject* ALearningAgentsManager::GetAgent(const int32 AgentId, const TSubclassOf<UObject> AgentClass)
{
	if (!OccupiedAgentSet.Contains(AgentId))
	{
		UE_LOG(LogLearning, Error, TEXT("%s: AgentId %d not found. Be sure to only use AgentIds returned by AddAgent() and check that the agent has not be removed."), *GetName(), AgentId);
		return nullptr;
	}

	return Agents[AgentId];
}

const UObject* ALearningAgentsManager::GetAgent(const int32 AgentId) const
{
	UE_LEARNING_CHECK(OccupiedAgentSet.Contains(AgentId));
	return Agents[AgentId];
}

UObject* ALearningAgentsManager::GetAgent(const int32 AgentId)
{
	UE_LEARNING_CHECK(OccupiedAgentSet.Contains(AgentId));
	return Agents[AgentId];
}

void ALearningAgentsManager::GetAgentIds(TArray<int32>& OutAgentIds) const
{
	OutAgentIds = OccupiedAgentIds;
}

void ALearningAgentsManager::UpdateAgentSets()
{
	OccupiedAgentSet = OccupiedAgentIds;
	OccupiedAgentSet.TryMakeSlice();
	VacantAgentSet = VacantAgentIds;
	VacantAgentSet.TryMakeSlice();
}
