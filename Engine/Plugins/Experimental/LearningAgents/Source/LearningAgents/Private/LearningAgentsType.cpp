// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsType.h"

#include "LearningAgentsManager.h"
#include "LearningAgentsObservations.h"
#include "LearningAgentsActions.h"

#include "LearningArray.h"
#include "LearningArrayMap.h"
#include "LearningFeatureObject.h"
#include "LearningLog.h"
#include "EngineDefines.h"

ULearningAgentsType::ULearningAgentsType() : ULearningAgentsManagerComponent() {}
ULearningAgentsType::ULearningAgentsType(FVTableHelper& Helper) : ULearningAgentsType() {}
ULearningAgentsType::~ULearningAgentsType() {}

void ULearningAgentsType::SetupAgentType(ALearningAgentsManager* InAgentManager)
{
	if (IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup already run!"), *GetName());
		return;
	}

	if (!InAgentManager)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: InAgentManager is nullptr."), *GetName());
		return;
	}

	if (!InAgentManager->IsManagerSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: %s's SetupManager must be run before it can be used."), *GetName(), *InAgentManager->GetName());
		return;
	}

	AgentManager = InAgentManager;

	// Setup Observations
	ObservationObjects.Empty();
	ObservationFeatures.Empty();
	SetupObservations(this);
	Observations = MakeShared<UE::Learning::FConcatenateFeature>(TEXT("Observations"),
		TLearningArrayView<1, const TSharedRef<UE::Learning::FFeatureObject>>(ObservationFeatures),
		InAgentManager->GetInstanceData().ToSharedRef(),
		InAgentManager->GetMaxInstanceNum());

	// Setup Actions
	ActionObjects.Empty();
	ActionFeatures.Empty();
	SetupActions(this);
	Actions = MakeShared<UE::Learning::FConcatenateFeature>(TEXT("Actions"),
		TLearningArrayView<1, const TSharedRef<UE::Learning::FFeatureObject>>(ActionFeatures),
		InAgentManager->GetInstanceData().ToSharedRef(),
		InAgentManager->GetMaxInstanceNum());

	bIsSetup = true;
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
	UE_LEARNING_CHECK(!IsSetup());
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
	UE_LEARNING_CHECK(!IsSetup());
	ActionObjects.Add(Object);
	ActionFeatures.Add(Feature);
}

void ULearningAgentsType::EncodeObservations()
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(ULearningAgentsType::EncodeObservations);

	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not run."), *GetName());
		return;
	}

	SetObservations(AddedAgentIds);

	Observations->Encode(AddedAgentSet);

#if ENABLE_VISUAL_LOG
	for (const ULearningAgentsObservation* ObservationObject : ObservationObjects)
	{
		if (ObservationObject)
		{
			ObservationObject->VisualLog(AddedAgentSet);
		}
	}
#endif
}

void ULearningAgentsType::DecodeActions()
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(ULearningAgentsType::DecodeActions);

	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not run."), *GetName());
		return;
	}

	Actions->Decode(AddedAgentSet);

	GetActions(AddedAgentIds);

#if ENABLE_VISUAL_LOG
	for (const ULearningAgentsAction* ActionObject : ActionObjects)
	{
		if (ActionObject)
		{
			ActionObject->VisualLog(AddedAgentSet);
		}
	}
#endif
}
void ULearningAgentsType::GetObservationVector(const int32 AgentId, TArray<float>& OutObservationVector) const
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not run."), *GetName());
		OutObservationVector.Empty();
		return;
	}

	if (!HasAgent(AgentId))
	{
		UE_LOG(LogLearning, Error, TEXT("%s: AgentId %d not found in the agents set."), *GetName(), AgentId);
		OutObservationVector.Empty();
		return;
	}

	OutObservationVector.SetNumUninitialized(Observations->DimNum());
	UE::Learning::Array::Copy<1, float>(OutObservationVector, Observations->FeatureBuffer()[AgentId]);
}

void ULearningAgentsType::GetActionVector(const int32 AgentId, TArray<float>& OutActionVector) const
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not run."), *GetName());
		OutActionVector.Empty();
		return;
	}

	if (!HasAgent(AgentId))
	{
		UE_LOG(LogLearning, Error, TEXT("%s: AgentId %d not found in the agents set."), *GetName(), AgentId);
		OutActionVector.Empty();
		return;
	}

	OutActionVector.SetNumUninitialized(Actions->DimNum());
	UE::Learning::Array::Copy<1, float>(OutActionVector, Actions->FeatureBuffer()[AgentId]);
}