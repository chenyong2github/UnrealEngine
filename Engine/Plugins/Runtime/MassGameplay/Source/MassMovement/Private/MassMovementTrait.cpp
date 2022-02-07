// Copyright Epic Games, Inc. All Rights Reserved.
#include "Movement/MassMovementTrait.h"
#include "MassEntityTemplateRegistry.h"
#include "MassCommonFragments.h"
#include "MassMovementFragments.h"
#include "MassMovementTypes.h"
#include "Engine/World.h"

void UMassMovementTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const
{
	UMassEntitySubsystem* EntitySubsystem = UWorld::GetSubsystem<UMassEntitySubsystem>(&World);
	check(EntitySubsystem);

	BuildContext.AddFragment<FAgentRadiusFragment>();
	BuildContext.AddFragment<FTransformFragment>();

	BuildContext.AddFragment<FMassVelocityFragment>();
	BuildContext.AddFragment<FMassForceFragment>();

	const FConstSharedStruct MovementFragment = EntitySubsystem->GetOrCreateConstSharedFragment(UE::StructUtils::GetStructCrc32(FConstStructView::Make(Movement)), Movement);
	BuildContext.AddConstSharedFragment(MovementFragment);
}
