// Copyright Epic Games, Inc. All Rights Reserved.

#include "Translators/MassTranslators_BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "MassEntitySubsystem.h"

UMassTranslator_BehaviorTree::UMassTranslator_BehaviorTree()
{
	bAutoRegisterWithProcessingPhases = false;
}

void UMassTranslator_BehaviorTree::ConfigureQueries()
{
	EntityQuery.AddRequirement<FDataFragment_BehaviorTreeComponentWrapper>(EMassFragmentAccess::ReadWrite);
}
