// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factories/EditorStaticMeshFactory.h"

#include "Elements/Framework/EngineElementsLibrary.h"
#include "Elements/Framework/TypedElementRegistry.h"
#include "Elements/Interfaces/TypedElementAssetDataInterface.h"
#include "Elements/Interfaces/TypedElementObjectInterface.h"

#include "ActorPartition/ActorPartitionSubsystem.h"
#include "InstancedFoliageActor.h"
#include "InstancedFoliage.h"
#include "FoliageType_InstancedStaticMesh.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"

#include "Subsystems/PlacementSubsystem.h"

TArray<FTypedElementHandle> UEditorStaticMeshFactory::PlaceAsset(const FAssetPlacementInfo& InPlacementInfo, const FPlacementOptions& InPlacementOptions)
{
	// If we're disallowing instanced placement, or creating preview elements, don't use the ISM placement.
	if (!ShouldPlaceInstancedStaticMeshes(InPlacementOptions))
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
			for (const auto& FoliageTypePair : FoliageActor->GetFoliageInfos())
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
			FoliageInfo->AddInstance(FoundOrCreatedType, FoliagePlacementInfo);
			CurrentPlacementScopedFoliageInfos.Add(FoliageInfo);

#if UE_ENABLE_SMINSTANCE_ELEMENTS
			return TArray<FTypedElementHandle>({ UEngineElementsLibrary::AcquireEditorSMInstanceElementHandle(FoliageInfo->GetComponent(), FoliageInfo->GetPlacedInstanceCount() - 1) });
#else
			return TArray<FTypedElementHandle>({ UEngineElementsLibrary::AcquireEditorComponentElementHandle(FoliageInfo->GetComponent()) });
#endif
		}
	}

	return TArray<FTypedElementHandle>();
}

FAssetData UEditorStaticMeshFactory::GetAssetDataFromElementHandle(const FTypedElementHandle& InHandle)
{
	UInstancedStaticMeshComponent* ISMComponent = nullptr;

	if (TTypedElement<UTypedElementObjectInterface> ObjectInterface = UTypedElementRegistry::GetInstance()->GetElement<UTypedElementObjectInterface>(InHandle))
	{
		// Try to pull from component handle
		if (UInstancedStaticMeshComponent* RawComponentPtr = ObjectInterface.GetObjectAs<UInstancedStaticMeshComponent>())
		{
			ISMComponent = RawComponentPtr;
		}
		else if (AActor* RawActorPtr = ObjectInterface.GetObjectAs<AActor>())
		{
			ISMComponent = RawActorPtr->FindComponentByClass<UInstancedStaticMeshComponent>();
		}
	}

	FAssetData FoundAssetData;
	if (ISMComponent)
	{
		FoundAssetData = FAssetData(ISMComponent->GetStaticMesh());
	}
	else if (TTypedElement<UTypedElementAssetDataInterface> AssetDataInterface = UTypedElementRegistry::GetInstance()->GetElement<UTypedElementAssetDataInterface>(InHandle))
	{
		FoundAssetData = AssetDataInterface.GetAssetData();
	}

	if (CanPlaceElementsFromAssetData(FoundAssetData))
	{
		return FoundAssetData;
	}

	// Todo: deal with instanced handles

	return Super::GetAssetDataFromElementHandle(InHandle);
}

void UEditorStaticMeshFactory::EndPlacement(TArrayView<const FTypedElementHandle> InPlacedElements, const FPlacementOptions& InPlacementOptions)
{
	if (ShouldPlaceInstancedStaticMeshes(InPlacementOptions))
	{
		for (FFoliageInfo* FoliageInfo : CurrentPlacementScopedFoliageInfos)
		{
			FoliageInfo->Refresh(true, false);
		}
	}

	CurrentPlacementScopedFoliageInfos.Empty();
}

bool UEditorStaticMeshFactory::ShouldPlaceInstancedStaticMeshes(const FPlacementOptions& InPlacementOptions) const
{
	return !InPlacementOptions.bIsCreatingPreviewElements && InPlacementOptions.bPreferInstancedPlacement;
}
