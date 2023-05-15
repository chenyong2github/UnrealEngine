// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/CollectionClothTetherBatchFacade.h"
#include "ChaosClothAsset/ClothCollection.h"

namespace UE::Chaos::ClothAsset
{
	int32 FCollectionClothTetherBatchConstFacade::GetNumTethers() const
	{
		return ClothCollection->GetNumElements(
			ClothCollection->GetTetherStart(),
			ClothCollection->GetTetherEnd(),
			GetElementIndex());
	}

	int32 FCollectionClothTetherBatchConstFacade::GetTethersOffset() const
	{
		return ClothCollection->GetElementsOffset(
			ClothCollection->GetTetherStart(),
			GetBaseElementIndex(),
			GetElementIndex());
	}

	TConstArrayView<int32> FCollectionClothTetherBatchConstFacade::GetTetherKinematicIndex() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetTetherKinematicIndex(),
			ClothCollection->GetTetherStart(),
			ClothCollection->GetTetherEnd(),
			GetElementIndex());
	}

	TConstArrayView<int32> FCollectionClothTetherBatchConstFacade::GetTetherDynamicIndex() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetTetherDynamicIndex(),
			ClothCollection->GetTetherStart(),
			ClothCollection->GetTetherEnd(),
			GetElementIndex());
	}

	TConstArrayView<float> FCollectionClothTetherBatchConstFacade::GetTetherReferenceLength() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetTetherReferenceLength(),
			ClothCollection->GetTetherStart(),
			ClothCollection->GetTetherEnd(),
			GetElementIndex());
	}

	TArray<TTuple<int32, int32, float>> FCollectionClothTetherBatchConstFacade::GetZippedTetherData() const
	{
		TArray<TTuple<int32, int32, float>> ZippedTethers;

		const int32 NumTethers = GetNumTethers();
		ZippedTethers.Reserve(NumTethers);

		const TConstArrayView<int32> TetherKinematicIndex = GetTetherKinematicIndex();
		const TConstArrayView<int32> TetherDynamicIndex = GetTetherDynamicIndex();
		const TConstArrayView<float> TetherReferenceLength = GetTetherReferenceLength();
		for (int32 TetherIndex = 0; TetherIndex < NumTethers; ++TetherIndex)
		{
			ZippedTethers.Emplace(TetherKinematicIndex[TetherIndex], TetherDynamicIndex[TetherIndex], TetherReferenceLength[TetherIndex]);
		}

		return ZippedTethers;
	}

	FCollectionClothTetherBatchConstFacade::FCollectionClothTetherBatchConstFacade(const TSharedPtr<const FClothCollection>& ClothCollection, int32 InLodIndex, int32 TetherBatchIndex)
		: ClothCollection(ClothCollection)
		, LodIndex(InLodIndex)
		, TetherBatchIndex(TetherBatchIndex)
	{
		check(ClothCollection.IsValid());
		check(ClothCollection->IsValid());
		check(LodIndex >= 0 && LodIndex < ClothCollection->GetNumElements(FClothCollection::LodsGroup));
		check(TetherBatchIndex >= 0 && TetherBatchIndex < ClothCollection->GetNumElements(ClothCollection->GetTetherBatchStart(), ClothCollection->GetTetherBatchEnd(), LodIndex));
	}

	int32 FCollectionClothTetherBatchConstFacade::GetBaseElementIndex() const
	{
		return (*ClothCollection->GetTetherBatchStart())[LodIndex];
	}

	void FCollectionClothTetherBatchFacade::Reset()
	{
		SetNumTethers(0);
		SetDefaults();
	}

	void FCollectionClothTetherBatchFacade::Initialize(const TArray<TTuple<int32, int32, float>>& Tethers)
	{
		Reset();

		const int32 NumTethers = Tethers.Num();

		SetNumTethers(NumTethers);

		const TArrayView<int32> TetherKinematicIndex = GetTetherKinematicIndex();
		const TArrayView<int32> TetherDynamicIndex = GetTetherDynamicIndex();
		const TArrayView<float> TetherReferenceLength = GetTetherReferenceLength();

		for (int32 TetherIndex = 0; TetherIndex < NumTethers; ++TetherIndex)
		{
			TetherKinematicIndex[TetherIndex] = Tethers[TetherIndex].Get<0>();
			TetherDynamicIndex[TetherIndex] = Tethers[TetherIndex].Get<1>();
			TetherReferenceLength[TetherIndex] = Tethers[TetherIndex].Get<2>();
		}
	}

	void FCollectionClothTetherBatchFacade::Initialize(const FCollectionClothTetherBatchConstFacade& Other)
	{
		Reset();

		//~ Tethers Group. 
		const int32 NumTethers = Other.GetNumTethers();
		SetNumTethers(NumTethers);
		
		const TConstArrayView<int32> OtherTetherKinematicIndex = Other.GetTetherKinematicIndex();
		const TConstArrayView<int32> OtherTetherDynamicIndex = Other.GetTetherDynamicIndex();
		const TConstArrayView<float> OtherTetherReferenceLength = Other.GetTetherReferenceLength();
		const TArrayView<int32> TetherKinematicIndex = GetTetherKinematicIndex();
		const TArrayView<int32> TetherDynamicIndex = GetTetherDynamicIndex();
		const TArrayView<float> TetherReferenceLength = GetTetherReferenceLength();

		for (int32 TetherIndex = 0; TetherIndex < NumTethers; ++TetherIndex)
		{
			TetherKinematicIndex[TetherIndex] = OtherTetherKinematicIndex[TetherIndex];
			TetherDynamicIndex[TetherIndex] = OtherTetherDynamicIndex[TetherIndex];
			TetherReferenceLength[TetherIndex] = OtherTetherReferenceLength[TetherIndex];
		}
	}

	void FCollectionClothTetherBatchFacade::SetNumTethers(int32 NumTethers)
	{
		GetClothCollection()->SetNumElements(
			NumTethers,
			FClothCollection::TethersGroup,
			GetClothCollection()->GetTetherStart(),
			GetClothCollection()->GetTetherEnd(),
			GetElementIndex());
	}

	TArrayView<int32> FCollectionClothTetherBatchFacade::GetTetherKinematicIndex()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetTetherKinematicIndex(),
			GetClothCollection()->GetTetherStart(),
			GetClothCollection()->GetTetherEnd(),
			GetElementIndex());
	}

	TArrayView<int32> FCollectionClothTetherBatchFacade::GetTetherDynamicIndex()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetTetherDynamicIndex(),
			GetClothCollection()->GetTetherStart(),
			GetClothCollection()->GetTetherEnd(),
			GetElementIndex());
	}

	TArrayView<float> FCollectionClothTetherBatchFacade::GetTetherReferenceLength()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetTetherReferenceLength(),
			GetClothCollection()->GetTetherStart(),
			GetClothCollection()->GetTetherEnd(),
			GetElementIndex());
	}

	FCollectionClothTetherBatchFacade::FCollectionClothTetherBatchFacade(const TSharedPtr<FClothCollection>& ClothCollection, int32 InLodIndex, int32 InTetherIndex)
		: FCollectionClothTetherBatchConstFacade(ClothCollection, InLodIndex, InTetherIndex)
	{
	}

	void FCollectionClothTetherBatchFacade::SetDefaults()
	{
		const int32 ElementIndex = GetElementIndex();

		(*GetClothCollection()->GetTetherStart())[ElementIndex] = INDEX_NONE;
		(*GetClothCollection()->GetTetherEnd())[ElementIndex] = INDEX_NONE;
	}
}  // End namespace UE::Chaos::ClothAsset
