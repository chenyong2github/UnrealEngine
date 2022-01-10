// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassZoneGraphMovementTrait.h"
#include "MassEntityTemplateRegistry.h"
#include "Engine/World.h"
#include "MassZoneGraphMovementFragments.h"
#include "MassCommonFragments.h"
#include "MassMovementSubsystem.h"
#include "MassMovementSettings.h"
#include "MassAIMovementFragments.h"
#include "VisualLogger/VisualLogger.h"

void UMassZoneGraphMovementTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const
{
	UMassMovementSubsystem* MovementSubsystem = World.GetSubsystem<UMassMovementSubsystem>();
	if (!MovementSubsystem)
	{
		UE_VLOG(&World, LogMassNavigation, Error, TEXT("Failed to get MassMovement Subsystem."));
		return;
	}

	BuildContext.AddFragment<FDataFragment_AgentRadius>();
	BuildContext.AddFragment<FDataFragment_Transform>();
	BuildContext.AddFragment<FMassVelocityFragment>();

	BuildContext.AddFragment<FMassMoveTargetFragment>();
	
	BuildContext.AddFragment<FMassZoneGraphLaneLocationFragment>();
	
	const UMassMovementSettings* Settings = GetDefault<UMassMovementSettings>();
	check(Settings);

	const FMassMovementConfigHandle ConfigHandle = Settings->GetMovementConfigHandleByID(Config.ID);
	FMassMovementConfigFragment& MovementConfigFragment = BuildContext.AddFragment_GetRef<FMassMovementConfigFragment>();
	MovementConfigFragment.ConfigHandle = ConfigHandle;

	BuildContext.AddFragment<FMassZoneGraphPathRequestFragment>();
	BuildContext.AddFragment<FMassZoneGraphShortPathFragment>();
	BuildContext.AddFragment<FMassZoneGraphCachedLaneFragment>();
	BuildContext.AddFragment<FMassSteeringFragment>();
	BuildContext.AddFragment<FMassSteeringGhostFragment>();
}
