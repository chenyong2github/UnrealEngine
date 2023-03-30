// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsType.h"
#include "LearningAgentsObservations.h"
#include "LearningAgentsActions.h"

#include "LearningArray.h"
#include "LearningArrayMap.h"
#include "LearningFeatureObject.h"
#include "LearningLog.h"
#include "LearningNeuralNetwork.h"
#include "LearningPolicyObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "EngineDefines.h"

ULearningAgentsType::ULearningAgentsType()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

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

void ULearningAgentsType::SetupAgentType(
	const FLearningAgentsTypeSettings& Settings,
	const FLearningAgentsNetworkSettings& NetworkSettings)
{
	// Reset Setup Flag
	bSetupPerformed = false;

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

	// Create Neural Network
	NeuralNetwork = MakeShared<UE::Learning::FNeuralNetwork>();
	NeuralNetwork->Resize(
		Observations->DimNum(), 
		2 * Actions->DimNum(), 
		NetworkSettings.HiddenLayerSize, 
		NetworkSettings.LayerNum);

	switch (NetworkSettings.ActivationFunction)
	{
		case ELearningAgentsActivationFunction::ReLU:
			NeuralNetwork->ActivationFunction = UE::Learning::EActivationFunction::ReLU;
			break;
		case ELearningAgentsActivationFunction::ELU:
			NeuralNetwork->ActivationFunction = UE::Learning::EActivationFunction::ELU;
			break;
		case ELearningAgentsActivationFunction::TanH:
			NeuralNetwork->ActivationFunction = UE::Learning::EActivationFunction::TanH;
			break;
		default:
			UE_LOG(LogLearning, Error, TEXT("NetworkSetting's activation function was not found. Defaulting to ELU."));
			NeuralNetwork->ActivationFunction = UE::Learning::EActivationFunction::ELU;
			break;
	}

	// Create Policy
	UE::Learning::FNeuralNetworkPolicyFunctionSettings PolicySettings;
	PolicySettings.ActionNoiseMin = Settings.ActionNoiseMin;
	PolicySettings.ActionNoiseMax = Settings.ActionNoiseMax;

	Policy = MakeUnique<UE::Learning::FNeuralNetworkPolicyFunction>(
		TEXT("Policy"), 
		InstanceData.ToSharedRef(), 
		MaxInstanceNum, 
		NeuralNetwork.ToSharedRef(), 
		Settings.ActionNoiseSeed,
		PolicySettings);

	InstanceData->Link(Observations->FeatureHandle, Policy->InputHandle);
	InstanceData->Link(Policy->OutputHandle, Actions->FeatureHandle);

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

UE::Learning::FNeuralNetwork& ULearningAgentsType::GetNeuralNetwork() const
{
	return *NeuralNetwork;
}

const UE::Learning::FNeuralNetworkPolicyFunction& ULearningAgentsType::GetPolicy() const
{
	return *Policy;
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

	OnAgentAdded.Broadcast(NewAgentId, Agent);

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

	OnAgentRemoved.Broadcast(AgentId, RemovedAgent);
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
	if (AgentId < 0 || AgentId >= Agents.Num())
	{
		UE_LOG(LogLearning, Warning, TEXT("AgentId %d outside valid range [0, %d]"), AgentId, Agents.Num() - 1);
		return nullptr;
	}

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
	check(!bSetupPerformed);
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
	check(!bSetupPerformed);
	ActionObjects.Add(Object);
	ActionFeatures.Add(Feature);
}

void ULearningAgentsType::LoadNetwork(const FDirectoryPath& Directory, const FString Filename)
{
	if (!bSetupPerformed)
	{
		UE_LOG(LogLearning, Error, TEXT("Setup must be run before network can be loaded."));
		return;
	}

	TArray64<uint8> NetworkData;
	FString FilePath = Directory.Path + FGenericPlatformMisc::GetDefaultPathSeparator() + Filename;
	if (FFileHelper::LoadFileToArray(NetworkData, *FilePath))
	{
		NeuralNetwork->DeserializeFromBytes(NetworkData);
		bNetworkLoaded = true;
	}
	else
	{
		UE_LOG(LogLearning, Warning, TEXT("Failed to load network. File not found: %s..."), *FilePath);
	}
}

void ULearningAgentsType::EncodeObservations()
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(ULearningAgentsType::EncodeObservations);

	if (!bSetupPerformed)
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


void ULearningAgentsType::EvaluatePolicy()
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(ULearningAgentsType::EvaluatePolicy);

	if (!bSetupPerformed)
	{
		UE_LOG(LogLearning, Error, TEXT("Setup must be run before the policy can be evaluated."));
		return;
	}

	Policy->Evaluate(OccupiedAgentSet);
}

void ULearningAgentsType::DecodeActions()
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(ULearningAgentsType::DecodeActions);

	if (!bSetupPerformed)
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

void ULearningAgentsType::BroadcastSetupComplete()
{
	OnSetupComplete.Broadcast();
}

void ULearningAgentsType::UpdateAgentSets()
{
	OccupiedAgentSet = OccupiedAgentIds;
	OccupiedAgentSet.TryMakeSlice();
	VacantAgentSet = VacantAgentIds;
	VacantAgentSet.TryMakeSlice();
}