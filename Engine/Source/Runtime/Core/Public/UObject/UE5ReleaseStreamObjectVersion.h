// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"
#include "Misc/Guid.h"

// Custom serialization version for changes made in //UE5/Release-* stream
struct CORE_API FUE5ReleaseStreamObjectVersion
{
	enum Type
	{
		// Before any version changes were made
		BeforeCustomVersionWasAdded = 0,

		// Added Lumen reflections to new reflection enum, changed defaults
		ReflectionMethodEnum,
		
		// Serialize HLOD info in WorldPartitionActorDesc
		WorldPartitionActorDescSerializeHLODInfo,

		// Removing Tessellation from materials and meshes.
		RemovingTessellation,

		// LevelInstance serialize runtime behavior
		LevelInstanceSerializeRuntimeBehavior,

		// Refactoring Pose Asset runtime data structures
		PoseAssetRuntimeRefactor,

		// Serialize the folder path of actor descs
		WorldPartitionActorDescSerializeActorFolderPath,

		// Change hair strands vertex format
		HairStrandsVertexFormatChange,
		
		// Added max linear and angular speed to Chaos bodies
		AddChaosMaxLinearAngularSpeed,

		// PackedLevelInstance version
		PackedLevelInstanceVersion,

		// PackedLevelInstance bounds fix
		PackedLevelInstanceBoundsFix,

		// -----<new versions can be added above this line>-------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	// The GUID for this custom version number
	const static FGuid GUID;

	FUE5ReleaseStreamObjectVersion() = delete;
};
