// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/CollectionClothFacade.h"
#include "ChaosClothAsset/CollectionClothLodFacade.h"
#include "ChaosClothAsset/ClothCollection.h"
#include "Chaos/ChaosArchive.h"
#include "UObject/UE5MainStreamObjectVersion.h"

namespace UE::Chaos::ClothAsset
{
	FCollectionClothConstFacade::FCollectionClothConstFacade(const TSharedPtr<const FManagedArrayCollection>& ManagedArrayCollection)
		: ClothCollection(MakeShared<const FClothCollection>(ConstCastSharedPtr<FManagedArrayCollection>(ManagedArrayCollection)))
	{
	}

	bool FCollectionClothConstFacade::IsValid() const
	{
		return ClothCollection->IsValid();
	}

	FCollectionClothLodConstFacade FCollectionClothConstFacade::GetLod(int32 LodIndex) const
	{
		check(LodIndex >= 0 && LodIndex < GetNumLods());
		return FCollectionClothLodConstFacade(ClothCollection, LodIndex);
	}

	int32 FCollectionClothConstFacade::GetNumLods() const
	{
		return ClothCollection->GetNumElements(FClothCollection::LodsGroup);
	}

	bool FCollectionClothConstFacade::HasWeightMap(const FName& Name) const
	{
		return ClothCollection->HasUserDefinedAttribute<float>(Name, FClothCollection::SimVerticesGroup);
	}

	TArray<FName> FCollectionClothConstFacade::GetWeightMapNames() const
	{
		return ClothCollection->GetUserDefinedAttributeNames<float>(FClothCollection::SimVerticesGroup);
	}

	FCollectionClothFacade::FCollectionClothFacade(const TSharedPtr<FManagedArrayCollection>& ManagedArrayCollection)
		: FCollectionClothConstFacade(ManagedArrayCollection)
	{
	}

	void FCollectionClothFacade::DefineSchema()
	{
		GetClothCollection()->DefineSchema();
	}

	void FCollectionClothFacade::Reset()
	{
		check(IsValid());

		const int32 NumLods = GetNumLods();
		for (int32 LodIndex = 0; LodIndex < NumLods; ++LodIndex)
		{
			GetLod(LodIndex).Reset();
		}

		GetClothCollection()->SetNumElements(0, FClothCollection::LodsGroup);
	}

	void FCollectionClothFacade::PostSerialize(const ::Chaos::FChaosArchive& Ar)
	{
		if (Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) < FUE5MainStreamObjectVersion::ClothCollectionTetherInitialization)
		{
			const int32 NumLods = GetNumLods();
			for (int32 LodIndex = 0; LodIndex < NumLods; ++LodIndex)
			{
				FCollectionClothLodFacade LodFacade = GetLod(LodIndex);
				const int32 NumTetherBatches = LodFacade.GetNumTetherBatches();
				for (int32 TetherBatchIndex = 0; TetherBatchIndex < NumTetherBatches; ++TetherBatchIndex)
				{
					// The tethers were never set to be empty, so do this manually.
					const int32 TetherBatchElementIndex = (*ClothCollection->GetTetherBatchStart())[LodIndex] + TetherBatchIndex;

					(*GetClothCollection()->GetTetherStart())[TetherBatchElementIndex] = INDEX_NONE;
					(*GetClothCollection()->GetTetherEnd())[TetherBatchElementIndex] = INDEX_NONE;
				}

				// Clear any (now empty) tether batches
				LodFacade.SetNumTetherBatches(0);
			}
		}
	}

	int32 FCollectionClothFacade::AddLod()
	{
		check(IsValid());

		const int32 LodIndex = GetClothCollection()->GetNumElements(FClothCollection::LodsGroup);
		GetClothCollection()->SetNumElements(LodIndex + 1, FClothCollection::LodsGroup);

		FCollectionClothLodFacade(GetClothCollection(), LodIndex).SetDefaults();

		return LodIndex;
	}

	FCollectionClothLodFacade FCollectionClothFacade::GetLod(int32 LodIndex)
	{
		check(IsValid());
		check(LodIndex < GetNumLods());
		return FCollectionClothLodFacade(GetClothCollection(), LodIndex);
	}

	void FCollectionClothFacade::SetNumLods(int32 InNumLods)
	{
		check(IsValid());

		const int32 NumLods = GetNumLods();

		for (int32 LodIndex = InNumLods; LodIndex < NumLods; ++LodIndex)
		{
			GetLod(LodIndex).Reset();
		}

		GetClothCollection()->SetNumElements(InNumLods, FClothCollection::LodsGroup);

		for (int32 LodIndex = NumLods; LodIndex < InNumLods; ++LodIndex)
		{
			GetLod(LodIndex).SetDefaults();
		}
	}

	void FCollectionClothFacade::AddWeightMap(const FName& Name)
	{
		check(IsValid());
		GetClothCollection()->AddUserDefinedAttribute<float>(Name, FClothCollection::SimVerticesGroup);
	}

	void FCollectionClothFacade::RemoveWeightMap(const FName& Name)
	{
		check(IsValid());
		GetClothCollection()->RemoveUserDefinedAttribute(Name, FClothCollection::SimVerticesGroup);
	}
} // End namespace UE::Chaos::ClothAsset
