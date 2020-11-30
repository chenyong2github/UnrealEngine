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

		// -----<new versions can be added above this line>-------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	// The GUID for this custom version number
	const static FGuid GUID;

	FUE5MainStreamObjectVersion() = delete;
};
