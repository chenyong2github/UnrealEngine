// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"

namespace UE::LevelSnapshots::Private
{
	struct LEVELSNAPSHOTS_API FSnapshotCustomVersion
	{
		enum Type
		{
			/** Before any version changes were made in the plugin */
			BeforeCustomVersionWasAdded = 0,

			/** When subobject support was added. Specifically, USceneComponent::AttachParent were not captured. */
			SubobjectSupport = 1,

			/** FSnapshotActorData now stores actor hash data to facilitate checking whether an actor has changed without loading the actor */
			ActorHash = 2,

			/** FWorldSnapshotData::ClassDefaults was replaced by FWorldSnapshotData::ClassData. */
			ClassArchetypeRefactor = 3,

			/** FWorldSnapshotData now compresses data using oodle before it is saved to disk */
			OoddleCompression = 4,

			// -----<new versions can be added above this line>-------------------------------------------------
			VersionPlusOne,
			LatestVersion = VersionPlusOne - 1
		};

		const static FGuid GUID;

		private:
		FSnapshotCustomVersion() = delete;
	};
}
