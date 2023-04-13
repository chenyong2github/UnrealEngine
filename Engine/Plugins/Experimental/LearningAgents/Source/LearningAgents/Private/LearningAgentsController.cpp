// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsController.h"

#include "LearningAgentsManager.h"
#include "LearningAgentsType.h"
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

void ULearningAgentsController::SetupController(ALearningAgentsManager* InAgentManager, ULearningAgentsType* InAgentType)
{
	if (IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup already performed!"), *GetName());
		return;
	}

	if (!InAgentManager)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: InAgentManager is nullptr."), *GetName());
		return;
	}

	if (!InAgentManager->IsManagerSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s's SetupManager() must be run before %s can be setup."), *InAgentManager->GetName(), *GetName());
		return;
	}

	// This manager is not referenced in this class but we need it in blueprints to call GetAgent()
	AgentManager = InAgentManager;

	if (!InAgentType)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: InAgentType is nullptr."), *GetName());
		return;
	}

	if (!InAgentType->IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: %s's Setup must be run before it can be used."), *GetName(), *InAgentType->GetName());
		return;
	}

	AgentType = InAgentType;

	bIsSetup = true;
}

void ULearningAgentsController::EncodeActions()
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(ULearningAgentsController::EncodeActions);

	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("Setup must be run before actions can be encoded."));
		return;
	}

	SetActions(AddedAgentIds);

	AgentType->GetActionFeature().Encode(AddedAgentSet);

#if ENABLE_VISUAL_LOG
	for (const ULearningAgentsAction* ActionObject : AgentType->GetActionObjects())
	{
		if (ActionObject)
		{
			ActionObject->VisualLog(AddedAgentSet);
		}
	}
#endif
}

ULearningAgentsType* ULearningAgentsController::GetAgentType(const TSubclassOf<ULearningAgentsType> AgentTypeClass) const
{
	if (!AgentType)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: AgentType is nullptr. Did we forget to call Setup on this component?"), *GetName());
		return nullptr;
	}

	return AgentType;
}
