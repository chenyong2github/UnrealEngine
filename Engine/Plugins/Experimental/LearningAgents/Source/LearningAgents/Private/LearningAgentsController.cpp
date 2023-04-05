// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsController.h"

#include "LearningAgentsType.h"
#include "LearningAgentsActions.h"
#include "LearningFeatureObject.h"
#include "LearningLog.h"

#include "EngineDefines.h"

ULearningAgentsController::ULearningAgentsController() : UActorComponent() {}
ULearningAgentsController::ULearningAgentsController(FVTableHelper& Helper) : ULearningAgentsController() {}
ULearningAgentsController::~ULearningAgentsController() {}

void ULearningAgentsController::SetActions_Implementation(const TArray<int32>& AgentIds)
{
	// Can be overridden to get actions without blueprints
}

void ULearningAgentsController::SetupController(ULearningAgentsType* InAgentType)
{
	if (IsControllerSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Setup already performed!"));
		return;
	}

	if (!InAgentType)
	{
		UE_LOG(LogLearning, Error, TEXT("SetupController called but AgentType is nullptr."));
		return;
	}

	if (!InAgentType->IsSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("AgentType Setup must be run before controller can be setup."));
		return;
	}

	AgentType = InAgentType;
}

bool ULearningAgentsController::IsControllerSetupPerformed() const
{
	return AgentType ? true : false;
}

void ULearningAgentsController::AddAgent(int32 AgentId)
{
	if (!IsControllerSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Controller setup must be run before agents can be added!"));
		return;
	}

	if (!AgentType->GetOccupiedAgentSet().Contains(AgentId))
	{
		UE_LOG(LogLearning, Error, TEXT("Unable to add: AgentId %d not found on AgentType. Make sure to add agents to the agent type before adding."), AgentId);
		return;
	}

	if (SelectedAgentIds.Contains(AgentId))
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId %i is already included in agents set"), AgentId);
		return;
	}

	SelectedAgentIds.Add(AgentId);
	SelectedAgentsSet = SelectedAgentIds;
	SelectedAgentsSet.TryMakeSlice();
}

void ULearningAgentsController::RemoveAgent(int32 AgentId)
{
	if (!IsControllerSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Controller setup must be run before agents can be removed!"));
		return;
	}

	if (SelectedAgentIds.RemoveSingleSwap(AgentId, false) == 0)
	{
		UE_LOG(LogLearning, Error, TEXT("Unable to remove: AgentId %d not found in the added agents set."), AgentId);
		return;
	}

	SelectedAgentsSet = SelectedAgentIds;
	SelectedAgentsSet.TryMakeSlice();
}

bool ULearningAgentsController::HasAgent(int32 AgentId) const
{
	return SelectedAgentsSet.Contains(AgentId);
}

ULearningAgentsType* ULearningAgentsController::GetAgentType(TSubclassOf<ULearningAgentsType> AgentClass)
{
	if (!IsControllerSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Controller setup must be run before getting the agent type!"));
		return nullptr;
	}

	return AgentType;
}

void ULearningAgentsController::EncodeActions()
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(ULearningAgentsController::EncodeActions);

	if (!IsControllerSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Setup must be run before actions can be encoded."));
		return;
	}

	SetActions(SelectedAgentIds);

	AgentType->GetActionFeature().Encode(SelectedAgentsSet);

#if ENABLE_VISUAL_LOG
	for (const ULearningAgentsAction* ActionObject : AgentType->GetActionObjects())
	{
		if (ActionObject)
		{
			ActionObject->VisualLog(SelectedAgentsSet);
		}
	}
#endif
}
