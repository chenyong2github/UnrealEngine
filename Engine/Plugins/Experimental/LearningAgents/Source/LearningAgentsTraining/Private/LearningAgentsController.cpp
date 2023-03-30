// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsController.h"

#include "LearningAgentsType.h"
#include "LearningFeatureObject.h"
#include "LearningLog.h"

ULearningAgentsController::ULearningAgentsController() : ULearningAgentsTypeComponent() {}
ULearningAgentsController::ULearningAgentsController(FVTableHelper& Helper) : ULearningAgentsController() {}
ULearningAgentsController::~ULearningAgentsController() {}

void ULearningAgentsController::SetActions_Implementation(const TArray<int32>& AgentIds)
{
	// Can be overridden to get actions without blueprints
}

void ULearningAgentsController::EncodeActions()
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(ULearningAgentsController::EncodeActions);

	SetActions(SelectedAgentIds);

	AgentType->GetActionFeature().Encode(SelectedAgentsSet);
}
