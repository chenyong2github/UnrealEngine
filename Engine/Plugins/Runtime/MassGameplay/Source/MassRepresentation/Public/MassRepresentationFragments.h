// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassLODTypes.h"
#include "MassEntityTypes.h"
#include "MassEntitySubsystem.h"
#include "MassActorSpawnerSubsystem.h"
#include "MassRepresentationTypes.h"

#include "MassRepresentationFragments.generated.h"

class UMassRepresentationSubsystem;
class UMassRepresentationActorManagement;

USTRUCT()
struct MASSREPRESENTATION_API FMassRepresentationLODFragment : public FMassFragment
{
	GENERATED_BODY()

	/** LOD information */
	TEnumAsByte<EMassLOD::Type> LOD = EMassLOD::Max;
	TEnumAsByte<EMassLOD::Type> PrevLOD = EMassLOD::Max;

	/** Visibility Info */
	EMassVisibility Visibility = EMassVisibility::Max;
	EMassVisibility PrevVisibility = EMassVisibility::Max;

	/** Value scaling from 0 to 3, 0 highest LOD we support and 3 being completely off LOD */
	float LODSignificance = 0.0f;
};

USTRUCT()
struct MASSREPRESENTATION_API FMassRepresentationFragment : public FMassFragment
{
	GENERATED_BODY()

	ERepresentationType CurrentRepresentation = ERepresentationType::None;

	ERepresentationType PrevRepresentation = ERepresentationType::None;

	int16 HighResTemplateActorIndex = INDEX_NONE;

	int16 LowResTemplateActorIndex = INDEX_NONE;

	int16 StaticMeshDescIndex = INDEX_NONE;

	FMassActorSpawnRequestHandle ActorSpawnRequestHandle;

	FTransform PrevTransform;

	/** Value scaling from 0 to 3, 0 highest LOD we support and 3 being completely off LOD */
	float PrevLODSignificance = -1.0f;
};

USTRUCT()
struct FMassRepresentationSubsystemFragment : public FMassSharedFragment
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	UMassRepresentationSubsystem* RepresentationSubsystem;
};

USTRUCT()
struct FMassRepresentationConfig : public FMassSharedFragment
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	UMassRepresentationActorManagement* RepresentationActorManagement;
};
