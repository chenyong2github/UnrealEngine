// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsType.h"
#include "LearningAgentsObservations.h"
#include "LearningAgentsActions.h"

#include "LearningArray.h"
#include "LearningArrayMap.h"
#include "LearningFeatureObject.h"
#include "LearningLog.h"
#include "EngineDefines.h"

ULearningAgentsType::ULearningAgentsType() : UActorComponent() {}
ULearningAgentsType::ULearningAgentsType(FVTableHelper& Helper) : ULearningAgentsType() {}
ULearningAgentsType::~ULearningAgentsType() {}

void ULearningAgentsType::PostInitProperties()
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

void ULearningAgentsType::SetupAgentType()
{
	if (IsSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Setup already performed!"));
		return;
	}

	// Allocate Instance Data
	InstanceData = MakeShared<UE::Learning::FArrayMap>();

	// Setup Observations
	ObservationObjects.Empty();
	ObservationFeatures.Empty();
	SetupObservations(this);
	Observations = MakeShared<UE::Learning::FConcatenateFeature>(TEXT("Observations"),
		TLearningArrayView<1, const TSharedRef<UE::Learning::FFeatureObject>>(ObservationFeatures),
		InstanceData.ToSharedRef(),
		MaxInstanceNum);

	// Setup Actions
	ActionObjects.Empty();
	ActionFeatures.Empty();
	SetupActions(this);
	Actions = MakeShared<UE::Learning::FConcatenateFeature>(TEXT("Actions"),
		TLearningArrayView<1, const TSharedRef<UE::Learning::FFeatureObject>>(ActionFeatures),
		InstanceData.ToSharedRef(),
		MaxInstanceNum);

	// Done!
	bSetupPerformed = true;
}

const bool ULearningAgentsType::IsSetupPerformed() const
{
	return bSetupPerformed;
}

const int32 ULearningAgentsType::GetMaxInstanceNum() const
{
	return MaxInstanceNum;
}

const TSharedPtr<UE::Learning::FArrayMap>& ULearningAgentsType::GetInstanceData() const
{
	return InstanceData;
}

UE::Learning::FFeatureObject& ULearningAgentsType::GetObservationFeature() const
{
	return *Observations;
}

UE::Learning::FFeatureObject& ULearningAgentsType::GetActionFeature() const
{
	return *Actions;
}

TConstArrayView<ULearningAgentsObservation*> ULearningAgentsType::GetObservationObjects() const
{
	return ObservationObjects;
}

TConstArrayView<ULearningAgentsAction*> ULearningAgentsType::GetActionObjects() const
{
	return ActionObjects;
}

const TConstArrayView<TObjectPtr<UObject>> ULearningAgentsType::GetAgents() const
{
	return Agents;
}

const UE::Learning::FIndexSet ULearningAgentsType::GetOccupiedAgentSet() const
{
	return OccupiedAgentSet;
}

const UE::Learning::FIndexSet ULearningAgentsType::GetVacantAgentSet() const
{
	return VacantAgentSet;
}

int32 ULearningAgentsType::AddAgent(UObject* Agent)
{
	if (Agent == nullptr)
	{
		UE_LOG(LogLearning, Error, TEXT("Attempted to add an agent but agent is nullptr."));
		return INDEX_NONE;
	}

	if (VacantAgentIds.IsEmpty())
	{
		UE_LOG(LogLearning, Error, TEXT("Attempting to add an agent but we have no more vacant ids. Increase MaxInstanceNum (%d) or remove unused agents."), Agents.Num());
		return INDEX_NONE;
	}

	// Add Agent
	const int32 NewAgentId = VacantAgentIds.Pop();
	Agents[NewAgentId] = Agent;

	OccupiedAgentIds.Add(NewAgentId);

	UpdateAgentSets();

	return NewAgentId;
}

void ULearningAgentsType::RemoveAgentById(int32 AgentId)
{
	if (AgentId == INDEX_NONE)
	{
		UE_LOG(LogLearning, Warning, TEXT("Attempting to remove an agent with id of INDEX_NONE."));
		return;
	}

	int32 RemovedCount = OccupiedAgentIds.RemoveSingleSwap(AgentId, false);

	if (RemovedCount == 0)
	{
		UE_LOG(LogLearning, Warning, TEXT("Trying to remove an agent but its Id (%d) is not in the occupied agents."), AgentId);
		return;
	}

	UObject* RemovedAgent = Agents[AgentId];

	// Remove Agent
	VacantAgentIds.Push(AgentId);
	Agents[AgentId] = nullptr;

	UpdateAgentSets();
}

void ULearningAgentsType::RemoveAgent(UObject* Agent)
{
	if (Agent == nullptr)
	{
		UE_LOG(LogLearning, Error, TEXT("Attempted to remove an agent but agent is nullptr."));
		return;
	}

	int32 AgentId = INDEX_NONE;
	bool bIsFound = Agents.Find(Agent, AgentId);

	if (!bIsFound)
	{
		UE_LOG(LogLearning, Warning, TEXT("Trying to remove an agent but it was not found."), AgentId);
		return;
	}

	RemoveAgentById(AgentId);
}

bool ULearningAgentsType::HasAgent(UObject* Agent) const
{
	return Agents.Find(Agent) ? true : false;
}

bool ULearningAgentsType::HasAgentById(int32 AgentId) const
{
	return OccupiedAgentSet.Contains(AgentId);
}

UObject* ULearningAgentsType::GetAgent(int32 AgentId, TSubclassOf<UObject> AgentClass)
{
	if (AgentId < 0 || AgentId >= Agents.Num())
	{
		UE_LOG(LogLearning, Warning, TEXT("AgentId %d outside valid range [0, %d]"), AgentId, Agents.Num() - 1);
		return nullptr;
	}

	return Agents[AgentId];
}

const UObject* ULearningAgentsType::GetAgent(int32 AgentId) const
{
	return Agents[AgentId];
}

UObject* ULearningAgentsType::GetAgent(int32 AgentId)
{
	return Agents[AgentId];
}

void ULearningAgentsType::SetupObservations_Implementation(ULearningAgentsType* AgentType)
{
	// Can be overridden to setup observations without blueprints
}

void ULearningAgentsType::SetObservations_Implementation(const TArray<int32>& AgentIds)
{
	// Can be overridden to set observations without blueprints
}

void ULearningAgentsType::AddObservation(TObjectPtr<ULearningAgentsObservation> Object, const TSharedRef<UE::Learning::FFeatureObject>& Feature)
{
	UE_LEARNING_CHECK(!IsSetupPerformed());
	ObservationObjects.Add(Object);
	ObservationFeatures.Add(Feature);
}

void ULearningAgentsType::SetupActions_Implementation(ULearningAgentsType* AgentType)
{
	// Can be overridden to setup actions without blueprints
}

void ULearningAgentsType::GetActions_Implementation(const TArray<int32>& AgentIds)
{
	// Can be overridden to get actions without blueprints
}

void ULearningAgentsType::AddAction(TObjectPtr<ULearningAgentsAction> Object, const TSharedRef<UE::Learning::FFeatureObject>& Feature)
{
	UE_LEARNING_CHECK(!IsSetupPerformed());
	ActionObjects.Add(Object);
	ActionFeatures.Add(Feature);
}

void ULearningAgentsType::EncodeObservations()
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(ULearningAgentsType::EncodeObservations);

	if (!IsSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Setup must be run before observations can be encoded."));
		return;
	}

	SetObservations(OccupiedAgentIds);

	Observations->Encode(OccupiedAgentSet);

#if ENABLE_VISUAL_LOG
	for (const ULearningAgentsObservation* ObservationObject : ObservationObjects)
	{
		if (ObservationObject)
		{
			ObservationObject->VisualLog(OccupiedAgentSet);
		}
	}
#endif
}

void ULearningAgentsType::DecodeActions()
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(ULearningAgentsType::DecodeActions);

	if (!IsSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Setup must be run before actions can be decoded."));
		return;
	}

	Actions->Decode(OccupiedAgentSet);

	GetActions(OccupiedAgentIds);

#if ENABLE_VISUAL_LOG
	for (const ULearningAgentsAction* ActionObject : ActionObjects)
	{
		if (ActionObject)
		{
			ActionObject->VisualLog(OccupiedAgentSet);
		}
	}
#endif
}

void ULearningAgentsType::UpdateAgentSets()
{
	OccupiedAgentSet = OccupiedAgentIds;
	OccupiedAgentSet.TryMakeSlice();
	VacantAgentSet = VacantAgentIds;
	VacantAgentSet.TryMakeSlice();
}