// Copyright Epic Games, Inc. All Rights Reserved.

#include "Steering/MassSteeringTrait.h"
#include "MassEntityTemplateRegistry.h"
#include "MassCommonFragments.h"
#include "MassMovementFragments.h"
#include "MassNavigationFragments.h"
#include "Steering/MassSteeringFragments.h"
#include "Engine/World.h"

void UMassSteeringTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const
{
	UMassEntitySubsystem* EntitySubsystem = UWorld::GetSubsystem<UMassEntitySubsystem>(&World);
	check(EntitySubsystem);

	BuildContext.AddFragment<FAgentRadiusFragment>();
	BuildContext.AddFragment<FTransformFragment>();
	BuildContext.AddFragment<FMassVelocityFragment>();
	BuildContext.AddFragment<FMassForceFragment>();
	BuildContext.AddFragment<FMassMoveTargetFragment>();
	
	BuildContext.AddFragment<FMassSteeringFragment>();
	BuildContext.AddFragment<FMassStandingSteeringFragment>();
	BuildContext.AddFragment<FMassGhostLocationFragment>();

	const FConstSharedStruct MovingSteeringFragment = EntitySubsystem->GetOrCreateConstSharedFragment(UE::StructUtils::GetStructCrc32(FConstStructView::Make(MovingSteering)), MovingSteering);
	BuildContext.AddConstSharedFragment(MovingSteeringFragment);

	const FConstSharedStruct StandingSteeringFragment = EntitySubsystem->GetOrCreateConstSharedFragment(UE::StructUtils::GetStructCrc32(FConstStructView::Make(StandingSteering)), StandingSteering);
	BuildContext.AddConstSharedFragment(StandingSteeringFragment);
}
