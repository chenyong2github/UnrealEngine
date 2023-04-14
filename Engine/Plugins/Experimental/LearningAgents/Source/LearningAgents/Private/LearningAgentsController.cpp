// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsController.h"

#include "LearningAgentsManager.h"
#include "LearningAgentsInteractor.h"
#include "LearningAgentsActions.h"
#include "LearningFeatureObject.h"
#include "LearningLog.h"

#include "EngineDefines.h"

ULearningAgentsController::ULearningAgentsController() : ULearningAgentsManagerComponent() {}
ULearningAgentsController::ULearningAgentsController(FVTableHelper& Helper) : ULearningAgentsController() {}
ULearningAgentsController::~ULearningAgentsController() {}

void ULearningAgentsController::SetActions_Implementation(const TArray<int32>& AgentIds)
{
	// Can be overridden to get actions without blueprints
}

void ULearningAgentsController::SetupController(ALearningAgentsManager* InAgentManager, ULearningAgentsInteractor* InInteractor)
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

	// This manager is not referenced in this class but we need it in blueprints to call GetAgent()
	AgentManager = InAgentManager;

	if (!InInteractor)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: InInteractor is nullptr."), *GetName());
		return;
	}

	if (!InInteractor->IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: %s's Setup must be run before it can be used."), *GetName(), *InInteractor->GetName());
		return;
	}

	Interactor = InInteractor;

	bIsSetup = true;
}

void ULearningAgentsController::EncodeActions()
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(ULearningAgentsController::EncodeActions);

	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not run."), *GetName());
		return;
	}

	SetActions(AddedAgentIds);

	Interactor->GetActionFeature().Encode(AddedAgentSet);

#if ENABLE_VISUAL_LOG
	for (const ULearningAgentsAction* ActionObject : Interactor->GetActionObjects())
	{
		if (ActionObject)
		{
			ActionObject->VisualLog(AddedAgentSet);
		}
	}
#endif
}

ULearningAgentsInteractor* ULearningAgentsController::GetInteractor(const TSubclassOf<ULearningAgentsInteractor> InteractorClass) const
{
	if (!Interactor)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Interactor is nullptr. Did we forget to call Setup on this component?"), *GetName());
		return nullptr;
	}

	return Interactor;
}

void ULearningAgentsController::RunController()
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(ULearningAgentsController::RunController);

	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not run."), *GetName());
		return;
	}

	Interactor->EncodeObservations();
	EncodeActions();
	Interactor->DecodeActions();
}
