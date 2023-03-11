// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/DataflowNodes/TerminalNode.h"
#include "ChaosClothAsset/DataflowNodes/DataflowNodes.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "Chaos/CollectionPropertyFacade.h"
#include "Animation/Skeleton.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Materials/Material.h"
#include "PhysicsEngine/PhysicsAsset.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TerminalNode)

#define LOCTEXT_NAMESPACE "ChaosClothAssetTerminalNode"

FChaosClothAssetTerminalNode::FChaosClothAssetTerminalNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowTerminalNode(InParam, InGuid)
{
	RegisterInputConnection(&CollectionLod0);
}

void FChaosClothAssetTerminalNode::SetAssetValue(TObjectPtr<UObject> Asset, Dataflow::FContext& Context) const
{
	if (UChaosClothAsset* ClothAsset = Cast<UChaosClothAsset>(Asset.Get()))
	{
		using namespace UE::Chaos::ClothAsset;

		// Reset the asset's collection
		const TSharedPtr<FManagedArrayCollection> ClothCollection = ClothAsset->GetClothCollection();
		ClothCollection->Reset();

		FCollectionClothFacade ClothFacade(ClothCollection);
		ClothFacade.DefineSchema();

		// Iterate through the LODs
		FString SkeletonPathName;
		FString PhysicsAssetPathName;

		const TArray<const FManagedArrayCollection*> CollectionLods = GetCollectionLods();
		for (int32 LodIndex = 0; LodIndex < CollectionLods.Num(); ++LodIndex)
		{
			// New LOD
			const int32 NewLodIndex = ClothFacade.AddLod();
			check(NewLodIndex == LodIndex);

			// Retrieve input LOD
			const FManagedArrayCollection& InCollectionLod = GetValue<FManagedArrayCollection>(Context, CollectionLods[LodIndex]);
			const TSharedRef<const FManagedArrayCollection> InClothCollection = MakeShared<const FManagedArrayCollection>(InCollectionLod);

			// Only use LOD 0 from input
			const FCollectionClothConstFacade InClothFacade(InClothCollection);
			if (InClothFacade.GetNumLods() >= 1)
			{
				const FCollectionClothLodConstFacade InClothLodFacade = InClothFacade.GetLod(0);

				// Copy input LOD 0 to current output LOD
				FCollectionClothLodFacade ClothLodFacade = ClothFacade.GetLod(LodIndex);
				ClothLodFacade.Initialize(InClothLodFacade);

				// Add this LOD's materials to the asset
				const int32 NumLodMaterials = InClothLodFacade.GetNumMaterials();

				TArray<FSkeletalMaterial>& Materials = ClothAsset->GetMaterials();
				Materials.Reserve(Materials.Num() + NumLodMaterials);

				const TConstArrayView<FString> LodRenderMaterialPathName = ClothLodFacade.GetRenderMaterialPathName();
				for (int32 LodMaterialIndex = 0; LodMaterialIndex < NumLodMaterials; ++LodMaterialIndex)
				{
					const FString& RenderMaterialPathName = LodRenderMaterialPathName[LodMaterialIndex];

					auto Predicate = [&RenderMaterialPathName](const FSkeletalMaterial& SkeletalMaterial)
						{
							return SkeletalMaterial.MaterialInterface && SkeletalMaterial.MaterialInterface->GetPathName() == RenderMaterialPathName;
						};

					if (!Materials.FindByPredicate(Predicate))
					{
						if (UMaterial* const Material = LoadObject<UMaterial>(ClothAsset, *RenderMaterialPathName, nullptr, LOAD_None, nullptr))
						{
							Materials.Emplace(Material, true, false, Material->GetFName());
						}
					}
				}

				// Set properties, skeleton, and physics asset only with LOD 0 at the moment
				if (LodIndex == 0)
				{
					using namespace ::Chaos::Softs;

					SkeletonPathName = InClothLodFacade.GetSkeletonAssetPathName();
					PhysicsAssetPathName = InClothLodFacade.GetPhysicsAssetPathName();
				
					FCollectionPropertyMutableFacade(ClothCollection).Append(*InClothCollection);
				}
			}
		}

		// Make sure that whatever happens there is always at least one empty LOD to avoid crashing the render data
		if (ClothFacade.GetNumLods() < 1)
		{
			ClothFacade.AddLod();
		}

		// Set reference skeleton
		constexpr bool bRebuildModels = false;  // Avoid rebuilding the asset twice
		USkeleton* const Skeleton = !SkeletonPathName.IsEmpty() ?
			LoadObject<USkeleton>(nullptr, *SkeletonPathName, nullptr, LOAD_None, nullptr) :
			nullptr;
		ClothAsset->SetSkeleton(Skeleton, bRebuildModels);

		// Set physics asset (note: the cloth asset's physics asset is only replaced if a collection path name is found valid)
		UPhysicsAsset* const PhysicsAsset = !PhysicsAssetPathName.IsEmpty() ?
			LoadObject<UPhysicsAsset>(ClothAsset, *PhysicsAssetPathName, nullptr, LOAD_None, nullptr) :
			nullptr;
		ClothAsset->SetPhysicsAsset(PhysicsAsset);

		// Rebuild the asset static data
		ClothAsset->Build();
	}
}

Dataflow::FPin FChaosClothAssetTerminalNode::AddPin()
{
	auto AddInput = [this](const FManagedArrayCollection* Collection) -> Dataflow::FPin
		{
			RegisterInputConnection(Collection);
			const FDataflowInput* const Input = FindInput(Collection);
			return { Dataflow::FPin::EDirection::INPUT, Input->GetType(), Input->GetName() };
		};

	switch (NumLods)
	{
	case 1: ++NumLods; return AddInput(&CollectionLod1);
	case 2: ++NumLods; return AddInput(&CollectionLod2);
	case 3: ++NumLods; return AddInput(&CollectionLod3);
	case 4: ++NumLods; return AddInput(&CollectionLod4);
	case 5: ++NumLods; return AddInput(&CollectionLod5);
	default: break;
	}

	return Super::AddPin();
}

Dataflow::FPin FChaosClothAssetTerminalNode::RemovePin()
{
	auto RemoveInput = [this](const FManagedArrayCollection* Collection) -> Dataflow::FPin
		{
			const FDataflowInput* const Input = FindInput(Collection);
			check(Input);
			Dataflow::FPin Pin = { Dataflow::FPin::EDirection::INPUT, Input->GetType(), Input->GetName() };
			UnregisterInputConnection(Collection);  // This will delete the input, so set the pin before that
			return Pin;
		};

	switch (NumLods - 1)
	{
	case 1: --NumLods; return RemoveInput(&CollectionLod1);
	case 2: --NumLods; return RemoveInput(&CollectionLod2);
	case 3: --NumLods; return RemoveInput(&CollectionLod3);
	case 4: --NumLods; return RemoveInput(&CollectionLod4);
	case 5: --NumLods; return RemoveInput(&CollectionLod5);
	default: break;
	}
	return Super::AddPin();
}

TArray<const FManagedArrayCollection*> FChaosClothAssetTerminalNode::GetCollectionLods() const
{
	TArray<const FManagedArrayCollection*> CollectionLods;
	CollectionLods.SetNumUninitialized(NumLods);

	for (int32 LodIndex = 0; LodIndex < NumLods; ++LodIndex)
	{
		switch (LodIndex)
		{
		case 0: CollectionLods[LodIndex] = &CollectionLod0; break;
		case 1: CollectionLods[LodIndex] = &CollectionLod1; break;
		case 2: CollectionLods[LodIndex] = &CollectionLod2; break;
		case 3: CollectionLods[LodIndex] = &CollectionLod3; break;
		case 4: CollectionLods[LodIndex] = &CollectionLod4; break;
		case 5: CollectionLods[LodIndex] = &CollectionLod5; break;
		default: check(false); break;
		}
	}
	return CollectionLods;
}

#undef LOCTEXT_NAMESPACE
