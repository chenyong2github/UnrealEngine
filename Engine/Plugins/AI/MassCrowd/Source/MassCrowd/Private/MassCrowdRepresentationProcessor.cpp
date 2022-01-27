// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassCrowdRepresentationProcessor.h"
#include "MassCrowdFragments.h"
#include "MassMovementFragments.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "Translators/MassCharacterMovementTranslators.h"
#include "MassAgentComponent.h"
#include "MassRepresentationSubsystem.h"

UMassCrowdRepresentationProcessor::UMassCrowdRepresentationProcessor()
{
	bAutoRegisterWithProcessingPhases = true;

	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::SyncWorldToMass);
}

void UMassCrowdRepresentationProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);
}


void UMassCrowdRepresentationProcessor::ConfigureQueries()
{
	Super::ConfigureQueries();
	EntityQuery.AddTagRequirement<FTagFragment_MassCrowd>(EMassFragmentPresence::All);
}
