// Copyright Epic Games, Inc. All Rights Reserved.
#include "SmoothOrientation/MassSmoothOrientationTrait.h"
#include "MassEntityTemplateRegistry.h"
#include "MassMovementFragments.h"
#include "MassCommonFragments.h"
#include "MassNavigationFragments.h"
#include "Engine/World.h"

void UMassSmoothOrientationTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const
{
	UMassEntitySubsystem* EntitySubsystem = UWorld::GetSubsystem<UMassEntitySubsystem>(&World);
	check(EntitySubsystem);

	BuildContext.AddFragment<FMassMoveTargetFragment>();
	BuildContext.AddFragment<FMassVelocityFragment>();
	BuildContext.AddFragment<FTransformFragment>();

	const FConstSharedStruct OrientationFragment = EntitySubsystem->GetOrCreateConstSharedFragment(UE::StructUtils::GetStructCrc32(FConstStructView::Make(Orientation)), Orientation);
	BuildContext.AddConstSharedFragment(OrientationFragment);
}
