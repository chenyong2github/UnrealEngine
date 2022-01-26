// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassCrowdVisualizationTrait.h"
#include "MassCrowdRepresentationSubsystem.h"
#include "MassCrowdVisualizationProcessor.h"
#include "MassCrowdRepresentationActorManagement.h"

UMassCrowdVisualizationTrait::UMassCrowdVisualizationTrait()
{
	// Override the subsystem to support parallelization of the crowd
	RepresentationSubsystemClass = UMassCrowdRepresentationSubsystem::StaticClass();
	Config.RepresentationActorManagementClass = UMassCrowdRepresentationActorManagement::StaticClass();
	Config.LODRepresentation[EMassLOD::High] = ERepresentationType::HighResSpawnedActor;
	Config.LODRepresentation[EMassLOD::Medium] = ERepresentationType::LowResSpawnedActor;
	Config.LODRepresentation[EMassLOD::Low] = ERepresentationType::StaticMeshInstance;
	Config.LODRepresentation[EMassLOD::Off] = ERepresentationType::None;
	// Set bKeepLowResActor to true as a spawning optimization, this will keep the low-res actor if available while showing the static mesh instance
	Config.bKeepLowResActors = true;
	Config.bKeepActorExtraFrame = true;
	Config.bSpreadFirstVisualizationUpdate = false;
	Config.WorldPartitionGridNameContainingCollision = NAME_None;
	Config.NotVisibleUpdateRate = 0.5f;
}
