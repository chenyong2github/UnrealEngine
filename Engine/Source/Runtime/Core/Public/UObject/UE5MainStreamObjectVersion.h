// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"
#include "Misc/Guid.h"

// Custom serialization version for changes made in //UE5/Main stream
struct CORE_API FUE5MainStreamObjectVersion
{
	enum Type
	{
		// Before any version changes were made
		BeforeCustomVersionWasAdded = 0,

		// Nanite data added to Chaos geometry collections
		GeometryCollectionNaniteData,

		// Nanite Geometry Collection data moved to DDC
		GeometryCollectionNaniteDDC,
		
		// Removing SourceAnimationData, animation layering is now applied during compression
		RemovingSourceAnimationData,

		// New MeshDescription format.
		// This is the correct versioning for MeshDescription changes which were added to ReleaseObjectVersion.
		MeshDescriptionNewFormat,

		// Serialize GridGuid in PartitionActorDesc
		PartitionActorDescSerializeGridGuid,

		// Set PKG_ContainsMapData on external actor packages
		ExternalActorsMapDataPackageFlag,

		// Added a new configurable BlendProfileMode that the user can setup to control the behavior of blend profiles.
		AnimationAddedBlendProfileModes,

		// Serialize DataLayers in WorldPartitionActorDesc
		WorldPartitionActorDescSerializeDataLayers,

		// Renaming UAnimSequence::NumFrames to NumberOfKeys, as that what is actually contains.
		RenamingAnimationNumFrames,

		// Serialize HLODLayer in WorldPartition HLODActorDesc
		WorldPartitionHLODActorDescSerializeHLODLayer,

		// Fixed Nanite Geometry Collection cooked data
		GeometryCollectionNaniteCooked,
			
		// Added bCooked to UFontFace assets
		AddedCookedBoolFontFaceAssets,

		// Serialize CellHash in WorldPartition HLODActorDesc
		WorldPartitionHLODActorDescSerializeCellHash,

		// Nanite data is now transient in Geometry Collection similar to how RenderData is transient in StaticMesh.
		GeometryCollectionNaniteTransient,

		// Added FLandscapeSplineActorDesc
		AddedLandscapeSplineActorDesc,

		// Added support for per-object collision constraint flag. [Chaos]
		AddCollisionConstraintFlag,

		// Initial Mantle Serialize Version
		MantleDbSerialize,

		// Animation sync groups explicitly specify sync method
		AnimSyncGroupsExplicitSyncMethod,

		// Fixup FLandscapeActorDesc Grid indices
		FLandscapeActorDescFixupGridIndices,

		// FoliageType with HLOD support
		FoliageTypeIncludeInHLOD,

		// Introducing UAnimDataModel sub-object for UAnimSequenceBase containing all animation source data
		IntroducingAnimationDataModel,

		// Serialize ActorLabel in WorldPartitionActorDesc
		WorldPartitionActorDescSerializeActorLabel,

		// Fix WorldPartitionActorDesc serialization archive not persistent
		WorldPartitionActorDescSerializeArchivePersistent,

		// Fix potentially duplicated actors when using ForceExternalActorLevelReference
		FixForceExternalActorLevelReferenceDuplicates,

		// Make UMeshDescriptionBase serializable
		SerializeMeshDescriptionBase,

		// Chaos FConvex uses array of FVec3s for vertices instead of particles
		ConvexUsesVerticesArray,

		// -----<new versions can be added above this line>-------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	// The GUID for this custom version number
	const static FGuid GUID;

	FUE5MainStreamObjectVersion() = delete;
};
