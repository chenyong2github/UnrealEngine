// Copyright Epic Games, Inc. All Rights Reserved.

#include "SnapshotComponentUtil.h"
#include "BaseComponentRestorer.h"

#include "Data/ActorSnapshotData.h"
#include "Data/WorldSnapshotData.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"

namespace LevelSnapshots
{
	/** Recreates component on actors in the snapshot world. */
	class FSnapshotActorComponentRestorer final : public TBaseComponentRestorer<FSnapshotActorComponentRestorer>
	{
	public:
		
		FSnapshotActorComponentRestorer(AActor* SnapshotActor, const FActorSnapshotData& SnapshotData, FWorldSnapshotData& WorldData)
			: TBaseComponentRestorer<FSnapshotActorComponentRestorer>(SnapshotActor, SnapshotData, WorldData)
		{}

		//~ Begin TBaseComponentRestorer Interface
		void PreCreateComponent(FName, UClass*, EComponentCreationMethod) const
		{}
		
		void PostCreateComponent(FSubobjectSnapshotData& SubobjectData, UActorComponent* RecreatedComponent) const
		{
			SubobjectData.SnapshotObject = RecreatedComponent;
		}
		
		static constexpr bool IsRestoringIntoSnapshotWorld()
		{
			return true;
		}
		//~ Begin TBaseComponentRestorer Interface
	};
	
}

void SnapshotUtil::Component::AllocateMissingComponentsForSnapshotActor(AActor* SnapshotActor, const FActorSnapshotData& SnapshotData, FWorldSnapshotData& WorldData)
{
	LevelSnapshots::FSnapshotActorComponentRestorer Restorer(SnapshotActor, SnapshotData, WorldData);
	Restorer.RecreateSavedComponents();
}