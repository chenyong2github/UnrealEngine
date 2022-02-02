// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassLODTypes.h"
#include "MassEntityTypes.h"
#include "MassEntitySubsystem.h"
#include "MassActorSpawnerSubsystem.h"
#include "MassRepresentationTypes.h"
#include "MassRepresentationActorManagement.h"

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

	EMassRepresentationType CurrentRepresentation = EMassRepresentationType::None;

	EMassRepresentationType PrevRepresentation = EMassRepresentationType::None;

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


	/** Allow subclasses to override the representation actor management behavior */
	UPROPERTY(EditAnywhere, Category = "Mass|Visual")
	TSubclassOf<UMassRepresentationActorManagement> RepresentationActorManagementClass;

	/** What should be the representation of this entity for each specific LOD */
	UPROPERTY(EditAnywhere, Category = "Mass|Representation", config)
	EMassRepresentationType LODRepresentation[EMassLOD::Max];

	/** If true, LowRes actors will be kept around, disabled, whilst StaticMeshInstance representation is active */
	UPROPERTY(EditAnywhere, Category = "Mass|Representation", config)
	bool bKeepLowResActors = true;

	/** When switching to ISM keep the actor an extra frame, helps cover rendering glitches (i.e. occlusion query being one frame late) */
	UPROPERTY(EditAnywhere, Category = "Mass|Representation", config)
	bool bKeepActorExtraFrame = false;

	/** If true, will spread the first visualization update over the period specified in NotVisibleUpdateRate member */
	UPROPERTY(EditAnywhere, Category = "Mass|Representation", config)
	bool bSpreadFirstVisualizationUpdate = false;

	/** World Partition grid name to test collision against, default None will be the main grid */
	UPROPERTY(EditAnywhere, Category = "Mass|Representation", config)
	FName WorldPartitionGridNameContainingCollision;

	/** At what rate should the not visible entity be updated in seconds */
	UPROPERTY(EditAnywhere, Category = "Mass|Visualization", config)
	float NotVisibleUpdateRate = 0.5f;

	inline void ComputeCachedValues() const;

	/** Default representation when unable to spawn an actor, gets calculated at initialization */
	UPROPERTY(Transient)
	mutable EMassRepresentationType CachedDefaultRepresentationType = EMassRepresentationType::None;

	UPROPERTY(Transient)
	mutable UMassRepresentationActorManagement* CachedRepresentationActorManagement = nullptr;
};

inline void FMassRepresentationConfig::ComputeCachedValues() const
{
	// Calculate the default representation when actor isn't spawned yet.
	for (int32 LOD = EMassLOD::High; LOD < EMassLOD::Max; LOD++)
	{
		// Find the first representation type after any actors
		if (LODRepresentation[LOD] == EMassRepresentationType::HighResSpawnedActor ||
			LODRepresentation[LOD] == EMassRepresentationType::LowResSpawnedActor)
		{
			continue;
		}

		CachedDefaultRepresentationType = LODRepresentation[LOD];
		break;
	}

	CachedRepresentationActorManagement = RepresentationActorManagementClass.GetDefaultObject();
	if (CachedRepresentationActorManagement == nullptr)
	{
		// We should have warn about it in the traits.
		CachedRepresentationActorManagement = UMassRepresentationActorManagement::StaticClass()->GetDefaultObject<UMassRepresentationActorManagement>();
	}
}
