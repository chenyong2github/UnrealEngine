// Copyright Epic Games, Inc. All Rights Reserved.

#include "SnapshotComponentUtil.h"
#include "BaseComponentRestorer.h"

#include "Data/ActorSnapshotData.h"
#include "Data/WorldSnapshotData.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"

namespace LevelSnapshots
{
	/** Recreates components on actors that were recreated in the editor world. */
	class FRecreatedActorComponentRestorer final : public TBaseComponentRestorer<FRecreatedActorComponentRestorer>
	{
	public:

		FRecreatedActorComponentRestorer(AActor* RecreatedEditorActor, const FActorSnapshotData& SnapshotData, FWorldSnapshotData& WorldData)
		: TBaseComponentRestorer<FRecreatedActorComponentRestorer>(RecreatedEditorActor, SnapshotData, WorldData)
		{}
		
		//~ Begin TBaseComponentRestorer Interface
		void PreCreateComponent(FName, UClass*, EComponentCreationMethod) const
		{}
		
		void PostCreateComponent(FSubobjectSnapshotData& SubobjectData, UActorComponent* RecreatedComponent) const
		{
			SubobjectData.EditorObject = RecreatedComponent;
			RecreatedComponent->RegisterComponent();
		}
		
		static constexpr bool IsRestoringIntoSnapshotWorld()
		{
			return false;
		}
		//~ Begin TBaseComponentRestorer Interface
	};
	
}

void SnapshotUtil::Component::AllocateMissingComponentsForRecreatedActor(AActor* RecreatedEditorActor, const FActorSnapshotData& SnapshotData, FWorldSnapshotData& WorldData)
{
	LevelSnapshots::FRecreatedActorComponentRestorer Restorer(RecreatedEditorActor, SnapshotData, WorldData);
	Restorer.RecreateSavedComponents();
}

