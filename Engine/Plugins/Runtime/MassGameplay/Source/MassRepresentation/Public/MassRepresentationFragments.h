// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassLODTypes.h"
#include "LWComponentTypes.h"
#include "MassEntitySubsystem.h"
#include "MassActorSpawnerSubsystem.h"
#include "MassRepresentationTypes.h"

#include "MassRepresentationFragments.generated.h"

USTRUCT()
struct MASSREPRESENTATION_API FMassRepresentationLODFragment : public FLWComponentData
{
	GENERATED_BODY()

	/** Saved closest ViewerDistance */
	float ClosestViewerDistanceSq = FLT_MAX;

	/** LOD information */
	TEnumAsByte<EMassLOD::Type> LOD = EMassLOD::Max;
	TEnumAsByte<EMassLOD::Type> PrevLOD = EMassLOD::Max;

	/** Visibility Info */
	bool bIsVisibleByAViewer = false;
	bool bWasVisibleByAViewer = false;
	bool bIsInVisibleRange = false;
	bool bWasInVisibleRange = false;

	/** Value scaling from 0 to 3, 0 highest LOD we support and 3 being completely off LOD */
	float LODSignificance = 0.0f;
};

USTRUCT()
struct MASSREPRESENTATION_API FMassRepresentationFragment : public FLWComponentData
{
	GENERATED_BODY()

	ERepresentationType CurrentRepresentation = ERepresentationType::None;

	ERepresentationType PrevRepresentation = ERepresentationType::None;

	int16 HighResTemplateActorIndex = INDEX_NONE;

	int16 LowResTemplateActorIndex = INDEX_NONE;

	int16 StaticMeshDescIndex = INDEX_NONE;

	FMassHandle_ActorSpawnRequest ActorSpawnRequestHandle;

	FTransform PrevTransform;

	// @todo: Remove this when we address character movement
	bool bControlCharacterMovement = true;
};