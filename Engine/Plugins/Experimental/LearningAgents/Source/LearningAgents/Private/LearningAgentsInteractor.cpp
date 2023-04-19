// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsInteractor.h"

#include "LearningAgentsManager.h"
#include "LearningAgentsObservations.h"
#include "LearningAgentsActions.h"

#include "LearningArray.h"
#include "LearningArrayMap.h"
#include "LearningFeatureObject.h"
#include "LearningLog.h"
#include "EngineDefines.h"

ULearningAgentsInteractor::ULearningAgentsInteractor() : ULearningAgentsManagerComponent() {}
ULearningAgentsInteractor::ULearningAgentsInteractor(FVTableHelper& Helper) : ULearningAgentsInteractor() {}
ULearningAgentsInteractor::~ULearningAgentsInteractor() {}

void ULearningAgentsInteractor::SetupInteractor(ALearningAgentsManager* InAgentManager)
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

	if (ObservationObjects.Num() == 0)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: No observations added to Interactor during SetupObservations."), *GetName());
		return;
	}

	if (Observations->DimNum() == 0)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Observation vector is zero-sized - all added observations have no size."), *GetName());
		return;
	}

	// Setup Actions
	ActionObjects.Empty();
	ActionFeatures.Empty();
	SetupActions(this);
	Actions = MakeShared<UE::Learning::FConcatenateFeature>(TEXT("Actions"),
		TLearningArrayView<1, const TSharedRef<UE::Learning::FFeatureObject>>(ActionFeatures),
		InAgentManager->GetInstanceData().ToSharedRef(),
		InAgentManager->GetMaxInstanceNum());

	if (ActionObjects.Num() == 0)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: No actions added to Interactor during SetupActions."), *GetName());
		return;
	}

	if (Actions->DimNum() == 0)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Action vector is zero-sized -  all added actions have no size."), *GetName());
		return;
	}

	bIsSetup = true;
}

UE::Learning::FFeatureObject& ULearningAgentsInteractor::GetObservationFeature() const
{
	return *Observations;
}

UE::Learning::FFeatureObject& ULearningAgentsInteractor::GetActionFeature() const
{
	return *Actions;
}

TConstArrayView<ULearningAgentsObservation*> ULearningAgentsInteractor::GetObservationObjects() const
{
	return ObservationObjects;
}

TConstArrayView<ULearningAgentsAction*> ULearningAgentsInteractor::GetActionObjects() const
{
	return ActionObjects;
}

void ULearningAgentsInteractor::SetupObservations_Implementation(ULearningAgentsInteractor* Interactor)
{
	// Can be overridden to setup observations without blueprints
}

void ULearningAgentsInteractor::SetObservations_Implementation(const TArray<int32>& AgentIds)
{
	// Can be overridden to set observations without blueprints
}

void ULearningAgentsInteractor::AddObservation(TObjectPtr<ULearningAgentsObservation> Object, const TSharedRef<UE::Learning::FFeatureObject>& Feature)
{
	UE_LEARNING_CHECK(!IsSetup());
	ObservationObjects.Add(Object);
	ObservationFeatures.Add(Feature);
}

void ULearningAgentsInteractor::SetupActions_Implementation(ULearningAgentsInteractor* Interactor)
{
	// Can be overridden to setup actions without blueprints
}

void ULearningAgentsInteractor::GetActions_Implementation(const TArray<int32>& AgentIds)
{
	// Can be overridden to get actions without blueprints
}

void ULearningAgentsInteractor::AddAction(TObjectPtr<ULearningAgentsAction> Object, const TSharedRef<UE::Learning::FFeatureObject>& Feature)
{
	UE_LEARNING_CHECK(!IsSetup());
	ActionObjects.Add(Object);
	ActionFeatures.Add(Feature);
}

void ULearningAgentsInteractor::EncodeObservations()
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(ULearningAgentsInteractor::EncodeObservations);

	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
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

void ULearningAgentsInteractor::DecodeActions()
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(ULearningAgentsInteractor::DecodeActions);

	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
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
void ULearningAgentsInteractor::GetObservationVector(const int32 AgentId, TArray<float>& OutObservationVector) const
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
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

void ULearningAgentsInteractor::GetActionVector(const int32 AgentId, TArray<float>& OutActionVector) const
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
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