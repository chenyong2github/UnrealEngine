// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/CollectionClothSeamFacade.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "ChaosClothAsset/ClothCollection.h"
#include "ChaosClothAsset/ClothGeometryTools.h"
#include "Containers/Map.h"

namespace UE::Chaos::ClothAsset
{
	namespace Private
	{
		typedef TMap<int32, int32> FWeldingGroup; // Key = Index, Value = Weight;

		static int32 WeldingMappedValue(const TMap<int32, int32>& WeldingMap, int32 Index)
		{
			const int32* Value = WeldingMap.Find(Index);
			if (!Value)
			{
				return Index;
			}
			return *Value;
		}

		static void UpdateWeldingMap(TMap<int32, int32>& WeldingMap, TMap<int32, FWeldingGroup>& WeldingGroups, int32 Index0, int32 Index1, const TConstArrayView<TArray<int32>>& SimVertex2DLookup)
		{
			// Update WeldingMap
			int32 Key0 = WeldingMappedValue(WeldingMap, Index0); // These might be swapped in the welding process
			int32 Key1 = WeldingMappedValue(WeldingMap, Index1);

			auto CountValidIndices = [](const TArray<int32>& Indices)
			{
				int32 Count = 0;
				for (int32 Index : Indices)
				{
					if (Index != INDEX_NONE)
					{
						++Count;
					}
				}
				return Count;
			};


			// Only process pairs that are not already redirected to the same index
			if (Key0 != Key1)
			{
				// Make sure Index0 points to the the smallest redirected index, so that merges are done into the correct group
				if (Key0 > Key1)
				{
					Swap(Key0, Key1);
					Swap(Index0, Index1);
				}

				// Find the group for Index0 if any
				FWeldingGroup* WeldingGroup0 = WeldingGroups.Find(Key0);
				if (!WeldingGroup0)
				{
					// No existing group, create a new one
					check(Key0 == Index0);  // No group means this index can't already have been redirected  // TODO: Make this a checkSlow
					const int32 Weight0 = CountValidIndices(SimVertex2DLookup[Index0]);
					check(Weight0 > 0);
					WeldingGroup0 = &WeldingGroups.Add(Key0);
					WeldingGroup0->Add({ Index0, Weight0 });
				}

				// Find the group for Index1, if it exists merge the two groups
				if (FWeldingGroup* const WeldingGroup1 = WeldingGroups.Find(Key1))
				{
					// Update group1 redirected indices with the new key
					for (TPair<int32, int32>& IndexAndWeight : *WeldingGroup1)
					{
						WeldingMap[IndexAndWeight.Get<0>()] = Key0;
					}

					// Merge group0 & group1
					WeldingGroup0->Append(*WeldingGroup1);

					// Remove group1
					WeldingGroups.Remove(Key1);

					// Sanity check
					check(WeldingGroup0->Contains(Key0) && WeldingGroup0->Contains(Key1));  // TODO: Make this a checkSlow
				}
				else
				{
					// Otherwise add Index1 to Index0's group
					check(Key1 == Index1);  // No group means this index can't already have been redirected  // TODO: Make this a checkSlow
					const int32 Weight1 = CountValidIndices(SimVertex2DLookup[Index1]);
					check(Weight1 > 0);
					WeldingMap.Add(Index1, Key0);
					WeldingGroup0->Add({ Index1, Weight1 });
				}
			}
		}

		// This is used for SimVertex3D <--> SimVertex2D, as well as SimVertex3D <--> SeamStitch
		static void UpdateWeldingLookups(const TMap<int32, FWeldingGroup>& WeldingGroups, const TArrayView<int32>& SimVertex3DLookup, const TArrayView<TArray<int32>>& ReverseLookup)
		{
			for (TMap<int32, FWeldingGroup>::TConstIterator GroupIter = WeldingGroups.CreateConstIterator(); GroupIter; ++GroupIter)
			{
				const int32 PrimaryIndex3D = GroupIter.Key();
				TArray<int32>& PrimaryReverseLookup = ReverseLookup[PrimaryIndex3D];
				for (const TPair<int32, int32>& IndexAndWeight : GroupIter.Value())
				{
					const TArray<int32>& MergingReverseLookup = ReverseLookup[IndexAndWeight.Get<0>()];
					for (const int32 ReverseIndex : MergingReverseLookup)
					{
						if (ReverseIndex != INDEX_NONE)
						{
							// All elements that used to point to us need to point to PrimaryIndex3D
							SimVertex3DLookup[ReverseIndex] = PrimaryIndex3D;
							PrimaryReverseLookup.AddUnique(ReverseIndex);
						}
					}
				}
			}
		}

		template<typename T>
		void WeldByWeightedAverage(const TMap<int32, FWeldingGroup>& WeldingGroups, const TArrayView<T>& Values)
		{
			for (TMap<int32, FWeldingGroup>::TConstIterator GroupIter = WeldingGroups.CreateConstIterator(); GroupIter; ++GroupIter)
			{
				T WeldedValue(0);
				int32 SourceCount = 0;
				for (const TPair<int32, int32>& IndexAndWeight : GroupIter.Value())
				{
					WeldedValue += Values[IndexAndWeight.Get<0>()] * IndexAndWeight.Get<1>();
					SourceCount += IndexAndWeight.Get<1>();
				}
				check(SourceCount > 0);
				Values[GroupIter.Key()] = WeldedValue / (float)SourceCount;
			}
		}

		static void WeldNormals(const TMap<int32, FWeldingGroup>& WeldingGroups, const TArrayView<FVector3f>& Normals)
		{
			for (TMap<int32, FWeldingGroup>::TConstIterator GroupIter = WeldingGroups.CreateConstIterator(); GroupIter; ++GroupIter)
			{
				FVector3f WeldedNormal(0);
				for (const TPair<int32, int32>& IndexAndWeight : GroupIter.Value())
				{
					WeldedNormal += Normals[IndexAndWeight.Get<0>()] * IndexAndWeight.Get<1>();
				}
				Normals[GroupIter.Key()] = WeldedNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector3f::XAxisVector);
			}
		}

		template<bool bNormalizeFloats, int8 MaxNumElements, typename FCompareFunc>
		static void WeldIndexAndFloatArrays(const TMap<int32, FWeldingGroup>& WeldingGroups, TArrayView<TArray<int32>> IndicesArray, TArrayView<TArray<float>> FloatsArray, FCompareFunc CompareFunc)
		{
			for (TMap<int32, FWeldingGroup>::TConstIterator GroupIter = WeldingGroups.CreateConstIterator(); GroupIter; ++GroupIter)
			{
				TMap<int32, TPair<float, int32>> WeldedData;
				for (const TPair<int32, int32>& IndexAndWeight : GroupIter.Value())
				{
					const TArray<int32>& Indices = IndicesArray[IndexAndWeight.Get<0>()];
					const TArray<float>& Floats = FloatsArray[IndexAndWeight.Get<0>()];
					check(Indices.Num() == Floats.Num());
					for (int32 Idx = 0; Idx < Indices.Num(); ++Idx)
					{
						TPair<float, int32>& WeightedFloat = WeldedData.FindOrAdd(Indices[Idx]);
						WeightedFloat.Get<0>() += Floats[Idx] * IndexAndWeight.Get<1>();
						WeightedFloat.Get<1>() += IndexAndWeight.Get<1>();
					}
				}
				TArray<int32>& IndicesToWrite = IndicesArray[GroupIter.Key()];
				TArray<float>& FloatsToWrite = FloatsArray[GroupIter.Key()];
				IndicesToWrite.Reset(WeldedData.Num());
				FloatsToWrite.Reset(WeldedData.Num());
				float FloatsSum = 0.f;
				for (TMap<int32, TPair<float, int32>>::TConstIterator WeldedDataIter = WeldedData.CreateConstIterator(); WeldedDataIter; ++WeldedDataIter)
				{
					check(WeldedDataIter.Value().Get<1>() > 0);
					IndicesToWrite.Add(WeldedDataIter.Key());
					const float FloatVal = WeldedDataIter.Value().Get<0>() / (float)WeldedDataIter.Value().Get<1>();
					FloatsToWrite.Add(FloatVal);
					FloatsSum += FloatVal;
				}
				if (IndicesToWrite.Num() > MaxNumElements)
				{
					TArray<TPair<float, int32>> SortableData;
					SortableData.Reserve(IndicesToWrite.Num());
					for (int32 Idx = 0; Idx < IndicesToWrite.Num(); ++Idx)
					{
						SortableData.Emplace(FloatsToWrite[Idx], IndicesToWrite[Idx]);
					}
					SortableData.Sort(CompareFunc);
					IndicesToWrite.SetNum(MaxNumElements);
					FloatsToWrite.SetNum(MaxNumElements);
					FloatsSum = 0.f;
					for (int32 Idx = 0; Idx < MaxNumElements; ++Idx)
					{
						IndicesToWrite[Idx] = SortableData[Idx].Get<1>();
						FloatsToWrite[Idx] = SortableData[Idx].Get<0>();
						FloatsSum += SortableData[Idx].Get<0>();
					}
				}

				if (bNormalizeFloats)
				{
					const float FloatsSumRecip = FloatsSum > UE_SMALL_NUMBER ? 1.f / FloatsSum : 0.f;
					for (float& Float : FloatsToWrite)
					{
						Float *= FloatsSumRecip;
					}
				}
			}
		}

		static void WeldTethers(const TMap<int32, int32>& WeldingMap, const TMap<int32, FWeldingGroup>& WeldingGroups, TArrayView<TArray<int32>> TetherKinematicIndices, TArrayView<TArray<float>> TetherReferenceLengths)
		{
			// Weld kinematic indices. Clean up any INDEX_NONEs out there created by removing 3d verts as well.
			// MaxNumAttachments in tether creation code is 4. Welding can introduce more than this,
			// but this is the magnitude of lengths we're talking about in these TArrays when we're 
			// doing things like linear lookups, resizes, etc.
			check(TetherKinematicIndices.Num() == TetherReferenceLengths.Num());
			const int32 NumVertices = TetherKinematicIndices.Num();
			for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
			{
				TArray<int32>& Indices = TetherKinematicIndices[VertexIndex];
				TArray<float>& Lengths = TetherReferenceLengths[VertexIndex];

				check(Indices.Num() == Lengths.Num());
				const int32 NumTethers = Indices.Num();
				// Go in reverse because we're going to remove any invalid tethers while we're here.
				for (int32 TetherIndex = NumTethers - 1; TetherIndex >= 0; --TetherIndex)
				{
					if (Indices[TetherIndex] == INDEX_NONE)
					{
						Indices.RemoveAtSwap(TetherIndex);
						Lengths.RemoveAtSwap(TetherIndex);
						continue;
					}
					const int32 MappedIndex = WeldingMappedValue(WeldingMap, Indices[TetherIndex]);
					if (MappedIndex != Indices[TetherIndex])
					{
						// Check if this mapped index is already a kinematic index
						const int32 MappedTetherIndex = Indices.Find(MappedIndex);
						if (MappedTetherIndex == INDEX_NONE)
						{
							// It doesn't. Just update the index.
							Indices[TetherIndex] = MappedIndex;
							continue;
						}
						// Merge the two tethers
						const FWeldingGroup& WeldingGroup = WeldingGroups.FindChecked(MappedIndex);
						const int32 WeightOrig = WeldingGroup.FindChecked(Indices[TetherIndex]);
						const int32 WeightMapped = WeldingGroup.FindChecked(MappedIndex);
						check(WeightOrig + WeightMapped > 0);
						Lengths[MappedTetherIndex] = (Lengths[TetherIndex] * WeightOrig + Lengths[MappedTetherIndex] * WeightMapped) / (WeightOrig + WeightMapped);
						Indices.RemoveAtSwap(TetherIndex);
						Lengths.RemoveAtSwap(TetherIndex);
					}
				}
			}

			// Now weld dynamic indices
			WeldIndexAndFloatArrays<false, FClothCollection::MaxNumTetherAttachments>(WeldingGroups, TetherKinematicIndices, TetherReferenceLengths,
				[](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A < B; });
		}

	} // namespace Private

	int32 FCollectionClothSeamConstFacade::GetNumSeamStitches() const
	{
		return ClothCollection->GetNumElements(
			ClothCollection->GetSeamStitchStart(),
			ClothCollection->GetSeamStitchEnd(),
			GetElementIndex());
	}

	int32 FCollectionClothSeamConstFacade::GetSeamStitchesOffset() const
	{
		return ClothCollection->GetElementsOffset(
			ClothCollection->GetSeamStitchStart(),
			GetBaseElementIndex(),
			GetElementIndex());
	}

	TConstArrayView<FIntVector2> FCollectionClothSeamConstFacade::GetSeamStitch2DEndIndices() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetSeamStitch2DEndIndices(),
			ClothCollection->GetSeamStitchStart(),
			ClothCollection->GetSeamStitchEnd(),
			GetElementIndex());
	}

	TConstArrayView<int32> FCollectionClothSeamConstFacade::GetSeamStitch3DIndex() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetSeamStitch3DIndex(),
			ClothCollection->GetSeamStitchStart(),
			ClothCollection->GetSeamStitchEnd(),
			GetElementIndex());
	}

	FCollectionClothSeamConstFacade::FCollectionClothSeamConstFacade(const TSharedPtr<const FClothCollection>& ClothCollection, int32 SeamIndex)
		: ClothCollection(ClothCollection)
		, SeamIndex(SeamIndex)
	{
		check(ClothCollection.IsValid());
		check(ClothCollection->IsValid());
		check(SeamIndex >= 0 && SeamIndex < ClothCollection->GetNumElements(FClothCollection::SeamsGroup));
	}

	void FCollectionClothSeamFacade::Reset()
	{
		FCollectionClothFacade Cloth(GetClothCollection());
		// Split all seams by duplicating points.
		// TODO: there is no way to remove seams without removing the associated vertices right now.
#if DO_CHECK // TODO: switch to GUARD_SLOW 
		const TConstArrayView<int32> SeamStitch3DIndex = GetSeamStitch3DIndex();
		for (int32 Idx : SeamStitch3DIndex)
		{
			check(Idx == INDEX_NONE);
		}
#endif
		
		SetNumSeamStitches(0);
		SetDefaults();
	}

	void FCollectionClothSeamFacade::Initialize(TConstArrayView<FIntVector2> InStitches)
	{
		using namespace Private;

		Reset();

		FCollectionClothFacade Cloth(GetClothCollection());
		const int32 NumSimVertices2D = Cloth.GetNumSimVertices2D();
		// Do not add stitches between the same vertices. These just make bookkeeping hard and do nothing.
		TArray<FIntVector2> Stitches;
		Stitches.Reserve(InStitches.Num());
		for (FIntVector2 Stitch : InStitches)
		{
			if (Stitch[0] >= 0 && Stitch[1] >= 0 && 
				Stitch[0] < NumSimVertices2D && Stitch[1] < NumSimVertices2D && 
				Stitch[0] != Stitch[1])
			{
				Stitches.Add(Stitch);
			}
		}

		const int32 NumStitches = Stitches.Num();

		SetNumSeamStitches(NumStitches);

		const TArrayView<FIntVector2> SeamStitch2DEndIndices = GetSeamStitch2DEndIndices();
		const TArrayView<int32> SeamStitch3DIndex = GetSeamStitch3DIndex();

		const TConstArrayView<int32> SimVertex3DLookup = Cloth.GetSimVertex3DLookup();
		const TConstArrayView<TArray<int32>> SimVertex2DLookup = Cloth.GetSimVertex2DLookup();

		// The welding map redirects to an existing vertex index if these two are part of the same welding group.
		// The redirected index must be the smallest index in the group. If a key is not in the WeldingMap, it redirects to itself.
		TMap<int32, int32> WeldingMap;
		WeldingMap.Reserve(NumStitches);

		// Define welding groups
		// Welding groups contain all stitched pair of indices to be welded together that are required to build the welding map.
		// Key is the smallest redirected index in the group, and will be the one index used in the welding map redirects.
		TMap<int32, FWeldingGroup> WeldingGroups;
		for (int32 StitchIndex = 0; StitchIndex < NumStitches; ++StitchIndex)
		{
			const FIntVector2& Stitch = Stitches[StitchIndex];
			const FIntVector2 Curr3DIndices(SimVertex3DLookup[Stitch[0]], SimVertex3DLookup[Stitch[1]]);

			// Copy stitch into our data
			SeamStitch2DEndIndices[StitchIndex] = Stitch;
			SeamStitch3DIndex[StitchIndex] = Curr3DIndices[0];

			UpdateWeldingMap(WeldingMap, WeldingGroups, Curr3DIndices[0], Curr3DIndices[1], SimVertex2DLookup);
		}
		
		// Update SeamStitch3DIndex with redirected values
		// Add SeamStitch to SeamStitchLookup (reverse lookup to SeamStitch3DIndex)
		const int32 StitchOffset = GetSeamStitchesOffset();
		TArrayView<TArray<int32>> SeamStitchLookup = Cloth.GetSeamStitchLookupPrivate();
		for (int32 StitchIndex = 0; StitchIndex < NumStitches; ++StitchIndex)
		{
			SeamStitch3DIndex[StitchIndex] = WeldingMappedValue(WeldingMap, SeamStitch3DIndex[StitchIndex]);
			SeamStitchLookup[SeamStitch3DIndex[StitchIndex]].Add(StitchOffset + StitchIndex);
		}

		if (WeldingMap.IsEmpty())
		{
			// Nothing actually got welded, so we're done.
			return;
		}

		// Update 2D vs 3D lookups
		UpdateWeldingLookups(WeldingGroups, Cloth.GetSimVertex3DLookupPrivate(), Cloth.GetSimVertex2DLookupPrivate());

		// Weld Stitch <-> 3D vertex lookups for stitches in other seams.
		UpdateWeldingLookups(WeldingGroups, GetClothCollection()->GetElements(GetClothCollection()->GetSeamStitch3DIndex()), Cloth.GetSeamStitchLookupPrivate());

		// Weld 3D positions
		WeldByWeightedAverage(WeldingGroups, Cloth.GetSimPosition3D());

		// Weld normals
		WeldNormals(WeldingGroups, Cloth.GetSimNormal());

		// Weld BoneIndices and Weights
		WeldIndexAndFloatArrays<true, FClothCollection::MaxNumBoneInfluences>(WeldingGroups, Cloth.GetSimBoneIndices(), Cloth.GetSimBoneWeights(),
			[](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A > B; });

		// Weld Tethers
		WeldTethers(WeldingMap, WeldingGroups, Cloth.GetTetherKinematicIndex(), Cloth.GetTetherReferenceLength());

		// Weld Faces
		// Just go through all faces and fix up. We could store vertex -> face lookups, but we'd have to ensure they stay in sync
		for (FIntVector3& Index3D : Cloth.GetSimIndices3D())
		{
			Index3D[0] = WeldingMappedValue(WeldingMap, Index3D[0]);
			Index3D[1] = WeldingMappedValue(WeldingMap, Index3D[1]);
			Index3D[2] = WeldingMappedValue(WeldingMap, Index3D[2]);
		}

		// Weld maps
		const TArray<FName> WeightMapNames = Cloth.GetWeightMapNames();
		for (const FName& WeightMapName : WeightMapNames)
		{
			WeldByWeightedAverage(WeldingGroups, Cloth.GetWeightMap(WeightMapName));
		}



		// Gather list of vertices to remove
		TArray<int32> VerticesToRemove;
		for (TMap<int32, int32>::TConstIterator WeldingMapIter = WeldingMap.CreateConstIterator(); WeldingMapIter; ++WeldingMapIter)
		{
			if (WeldingMapIter.Key() != WeldingMapIter.Value())
			{
				VerticesToRemove.Add(WeldingMapIter.Key());
			}
		}
		VerticesToRemove.Sort();
		GetClothCollection()->RemoveElements(FClothCollection::SimVertices3DGroup, VerticesToRemove);		
	}

	void FCollectionClothSeamFacade::Initialize(const FCollectionClothSeamConstFacade& Other, const int32 SimVertex2DOffset, const int32 SimVertex3DOffset)
	{
		SetNumSeamStitches(Other.GetNumSeamStitches());
		FClothCollection::CopyArrayViewDataAndApplyOffset(GetSeamStitch2DEndIndices(), Other.GetSeamStitch2DEndIndices(), FIntVector2(SimVertex2DOffset));
		FClothCollection::CopyArrayViewDataAndApplyOffset(GetSeamStitch3DIndex(), Other.GetSeamStitch3DIndex(), SimVertex3DOffset);
	}

	void FCollectionClothSeamFacade::SetNumSeamStitches(int32 NumStitches)
	{
		GetClothCollection()->SetNumElements(
			NumStitches,
			FClothCollection::SeamStitchesGroup,
			GetClothCollection()->GetSeamStitchStart(),
			GetClothCollection()->GetSeamStitchEnd(),
			GetElementIndex());
	}

	TArrayView<FIntVector2> FCollectionClothSeamFacade::GetSeamStitch2DEndIndices()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetSeamStitch2DEndIndices(),
			GetClothCollection()->GetSeamStitchStart(),
			GetClothCollection()->GetSeamStitchEnd(),
			GetElementIndex());
	}

	TArrayView<int32> FCollectionClothSeamFacade::GetSeamStitch3DIndex()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetSeamStitch3DIndex(),
			GetClothCollection()->GetSeamStitchStart(),
			GetClothCollection()->GetSeamStitchEnd(),
			GetElementIndex());
	}

	FCollectionClothSeamFacade::FCollectionClothSeamFacade(const TSharedPtr<FClothCollection>& ClothCollection, int32 InSeamIndex)
		: FCollectionClothSeamConstFacade(ClothCollection, InSeamIndex)
	{
	}

	void FCollectionClothSeamFacade::SetDefaults()
	{
		const int32 ElementIndex = GetElementIndex();

		(*GetClothCollection()->GetSeamStitchStart())[ElementIndex] = INDEX_NONE;
		(*GetClothCollection()->GetSeamStitchEnd())[ElementIndex] = INDEX_NONE;
	}
}  // End namespace UE::Chaos::ClothAsset
