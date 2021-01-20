// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factories/EditorStaticMeshFactory.h"

#include "Elements/Actor/ActorElementData.h"
#include "Elements/Component/ComponentElementData.h"
#include "Elements/Object/ObjectElementData.h"
#include "Elements/Framework/EngineElementsLibrary.h"

#include "ActorPartition/ActorPartitionSubsystem.h"
#include "InstancedFoliageActor.h"
#include "InstancedFoliage.h"
#include "FoliageType_InstancedStaticMesh.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"

#include "Subsystems/PlacementSubsystem.h"

TArray<FTypedElementHandle> UEditorStaticMeshFactory::PlaceAsset(const FAssetPlacementInfo& InPlacementInfo, const FPlacementOptions& InPlacementOptions)
{
	if (!InPlacementOptions.bPreferInstancedPlacement)
	{
		return Super::PlaceAsset(InPlacementInfo, InPlacementOptions);
	}

	if (InPlacementInfo.PreferredLevel.IsValid())
	{
		if (UActorPartitionSubsystem* PartitionSubsystem = UWorld::GetSubsystem<UActorPartitionSubsystem>(InPlacementInfo.PreferredLevel->GetWorld()))
		{
			// Create or find the foliage partition actor
			constexpr bool bCreatePartitionActorIfMissing = true;
			FActorPartitionGetParams PartitionActorFindParams(AInstancedFoliageActor::StaticClass(), bCreatePartitionActorIfMissing, InPlacementInfo.PreferredLevel.Get(), InPlacementInfo.FinalizedTransform.GetLocation());
			AInstancedFoliageActor* FoliageActor = Cast<AInstancedFoliageActor>(PartitionSubsystem->GetActor(PartitionActorFindParams));
			check(FoliageActor);

			// Create or find the instanced static mesh foliage type for the given mesh
			UObject* AssetDataObject = InPlacementInfo.AssetToPlace.GetAsset();
			UFoliageType_InstancedStaticMesh* FoundOrCreatedType = nullptr;
			for (auto& FoliageTypePair : FoliageActor->FoliageInfos)
			{
				FAssetData FoliageTypeSource(FoliageTypePair.Key->GetSource());
				if (FoliageTypeSource == InPlacementInfo.AssetToPlace)
				{
					FoundOrCreatedType = Cast<UFoliageType_InstancedStaticMesh>(FoliageTypePair.Key);
					break;
				}
			}
			if (!FoundOrCreatedType)
			{
				FoundOrCreatedType = NewObject<UFoliageType_InstancedStaticMesh>(FoliageActor);
				FoundOrCreatedType->SetSource(InPlacementInfo.AssetToPlace.GetAsset());
			}
			FFoliageInfo* FoliageInfo = FoliageActor->FindOrAddMesh(FoundOrCreatedType);
			check(FoliageInfo);

			FDesiredFoliageInstance InstancePlaceInfo;
			FFoliageInstance FoliagePlacementInfo;
			FoliagePlacementInfo.Location = InPlacementInfo.FinalizedTransform.GetLocation();
			FoliagePlacementInfo.Rotation = InPlacementInfo.FinalizedTransform.Rotator();
			FoliagePlacementInfo.DrawScale3D = InPlacementInfo.FinalizedTransform.GetScale3D();
			FoliageInfo->AddInstance(FoliageActor, FoundOrCreatedType, FoliagePlacementInfo);

			// Todo: deal with returning instanced handles
			// In the meantime, we return the handle of the the HISM component added.
			return TArray<FTypedElementHandle>({ UEngineElementsLibrary::AcquireEditorComponentElementHandle(FoliageInfo->GetComponent()) });
		}
	}

	return TArray<FTypedElementHandle>();
}

FAssetData UEditorStaticMeshFactory::GetAssetDataFromElementHandle(const FTypedElementHandle& InHandle)
{
	UInstancedStaticMeshComponent* ISMComponent = nullptr;

	// Try to pull from component handle
	const FComponentElementData* ComponentData = InHandle.GetData<FComponentElementData>();
	if (ComponentData && ComponentData->Component)
	{
		ISMComponent = Cast<UInstancedStaticMeshComponent>(ComponentData->Component);
	}
	else
	{
		// Try to pull from actor handle
		const FActorElementData* ActorData = InHandle.GetData<FActorElementData>();
		if (ActorData && ActorData->Actor)
		{
			ISMComponent = ActorData->Actor->FindComponentByClass<UInstancedStaticMeshComponent>();
		}
	}

	FAssetData FoundAssetData;
	if (ISMComponent)
	{
		FoundAssetData = FAssetData(ISMComponent->GetStaticMesh());
	}
	else if (const FObjectElementData* ObjectData = InHandle.GetData<FObjectElementData>())
	{
		if (ObjectData && ObjectData->Object)
		{
			FoundAssetData = FAssetData(ObjectData->Object);
		}
	}

	if (CanPlaceElementsFromAssetData(FoundAssetData))
	{
		return FoundAssetData;
	}

	// Todo: deal with instanced handles

	return Super::GetAssetDataFromElementHandle(InHandle);
}
