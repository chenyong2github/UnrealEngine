// Copyright Epic Games, Inc. All Rights Reserved.

#include "Avoidance/MassAvoidanceTrait.h"
#include "Avoidance/MassAvoidanceFragments.h"
#include "MassEntityTemplateRegistry.h"
#include "MassMovementFragments.h"
#include "MassCommonFragments.h"
#include "MassNavigationFragments.h"
#include "Engine/World.h"
#include "MassEntityUtils.h"


void UMassObstacleAvoidanceTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	FMassEntityManager& EntityManager = UE::Mass::Utils::GetEntityManagerChecked(World);

	BuildContext.RequireFragment<FAgentRadiusFragment>();
	BuildContext.AddFragment<FMassNavigationEdgesFragment>();
	BuildContext.RequireFragment<FTransformFragment>();
	BuildContext.RequireFragment<FMassVelocityFragment>();
	BuildContext.RequireFragment<FMassForceFragment>();
	BuildContext.RequireFragment<FMassMoveTargetFragment>();

	const FMassMovingAvoidanceParameters MovingValidated = MovingParameters.GetValidated();
	const uint32 MovingHash = UE::StructUtils::GetStructCrc32(FConstStructView::Make(MovingValidated));
	const FConstSharedStruct MovingFragment = EntityManager.GetOrCreateConstSharedFragment(MovingHash, MovingValidated);
	BuildContext.AddConstSharedFragment(MovingFragment);

	const FMassStandingAvoidanceParameters StandingValidated = StandingParameters.GetValidated();
	const uint32 StandingHash = UE::StructUtils::GetStructCrc32(FConstStructView::Make(StandingValidated));
	const FConstSharedStruct StandingFragment = EntityManager.GetOrCreateConstSharedFragment(StandingHash, StandingValidated);
	BuildContext.AddConstSharedFragment(StandingFragment);
}
