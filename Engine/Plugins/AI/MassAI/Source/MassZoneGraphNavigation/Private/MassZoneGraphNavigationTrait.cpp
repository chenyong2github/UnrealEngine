// Copyright Epic Games, Inc. All Rights Reserved.
#include "MassZoneGraphNavigationTrait.h"
#include "MassEntityTemplateRegistry.h"
#include "MassCommonFragments.h"
#include "MassMovementFragments.h"
#include "MassNavigationFragments.h"
#include "MassZoneGraphNavigationFragments.h"
#include "Engine/World.h"

void UMassZoneGraphNavigationTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const
{
	UMassEntitySubsystem* EntitySubsystem = UWorld::GetSubsystem<UMassEntitySubsystem>(&World);
	check(EntitySubsystem);

	BuildContext.AddFragment<FAgentRadiusFragment>();
	BuildContext.AddFragment<FTransformFragment>();
	BuildContext.AddFragment<FMassVelocityFragment>();
	BuildContext.AddFragment<FMassMoveTargetFragment>();

	BuildContext.AddFragment<FMassZoneGraphLaneLocationFragment>();
	BuildContext.AddFragment<FMassZoneGraphPathRequestFragment>();
	BuildContext.AddFragment<FMassZoneGraphShortPathFragment>();
	BuildContext.AddFragment<FMassZoneGraphCachedLaneFragment>();

	const FConstSharedStruct ZGMovementParamsFragment = EntitySubsystem->GetOrCreateConstSharedFragment(UE::StructUtils::GetStructCrc32(FConstStructView::Make(NavigationParameters)), NavigationParameters);
	BuildContext.AddConstSharedFragment(ZGMovementParamsFragment);
}
