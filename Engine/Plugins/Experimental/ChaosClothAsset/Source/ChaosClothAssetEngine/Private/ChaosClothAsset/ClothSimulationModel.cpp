// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothSimulationModel.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "ReferenceSkeleton.h"

namespace UE::Chaos::ClothAsset
{
	// Find the root bone for this cloth asset (common bone for all used bones)
	static int32 CalculateReferenceBoneIndex(const TArray<FChaosClothSimulationLodModel> ClothSimulationLodModels, const FReferenceSkeleton& ReferenceSkeleton, const TArray<int32>& UsedBoneIndices)
	{
		// Starts at root
		int32 ReferenceBoneIndex = 0;

		// List of valid paths to the root bone from each weighted bone
		TArray<TArray<int32>> PathsToRoot;

		// First build a list per used bone for it's path to root
		TArray<int32> WeightedBones;  // List of actually weighted (not just used) bones

		for (const FChaosClothSimulationLodModel& ClothSimulationLodModel : ClothSimulationLodModels)
		{
			for (const FClothVertBoneData& VertBoneData : ClothSimulationLodModel.BoneData)
			{
				for (int32 InfluenceIndex = 0; InfluenceIndex < VertBoneData.NumInfluences; ++InfluenceIndex)
				{
					if (VertBoneData.BoneWeights[InfluenceIndex] > SMALL_NUMBER)
					{
						const int32 UnmappedBoneIndex = VertBoneData.BoneIndices[InfluenceIndex];
						check(UsedBoneIndices.IsValidIndex(UnmappedBoneIndex));
						WeightedBones.AddUnique(UsedBoneIndices[UnmappedBoneIndex]);
					}
					else
					{
						// Hit the last weight (they're sorted)
						break;
					}
				}
			}
		}

		const int32 NumWeightedBones = WeightedBones.Num();
		PathsToRoot.Reserve(NumWeightedBones);

		// Compute paths to the root bone
		for (int32 WeightedBoneIndex = 0; WeightedBoneIndex < NumWeightedBones; ++WeightedBoneIndex)
		{
			PathsToRoot.AddDefaulted();
			TArray<int32>& Path = PathsToRoot.Last();

			int32 CurrentBone = WeightedBones[WeightedBoneIndex];
			Path.Add(CurrentBone);

			while (CurrentBone != 0 && CurrentBone != INDEX_NONE)
			{
				CurrentBone = ReferenceSkeleton.GetParentIndex(CurrentBone);
				Path.Add(CurrentBone);
			}
		}

		// Paths are from leaf->root, we want the other way
		for (TArray<int32>& Path : PathsToRoot)
		{
			Algo::Reverse(Path);
		}

		// Verify the last common bone in all paths as the root of the sim space
		const int32 NumPaths = PathsToRoot.Num();
		if (NumPaths > 0)
		{
			TArray<int32>& FirstPath = PathsToRoot[0];

			const int32 FirstPathSize = FirstPath.Num();
			for (int32 PathEntryIndex = 0; PathEntryIndex < FirstPathSize; ++PathEntryIndex)
			{
				const int32 CurrentQueryIndex = FirstPath[PathEntryIndex];
				bool bValidRoot = true;

				for (int32 PathIndex = 1; PathIndex < NumPaths; ++PathIndex)
				{
					if (!PathsToRoot[PathIndex].Contains(CurrentQueryIndex))
					{
						bValidRoot = false;
						break;
					}
				}

				if (bValidRoot)
				{
					ReferenceBoneIndex = CurrentQueryIndex;
				}
				else
				{
					// Once we fail to find a valid root we're done.
					break;
				}
			}
		}
		else
		{
			// Just use the root
			ReferenceBoneIndex = 0;
		}
		return ReferenceBoneIndex;
	}

	// TODO: Check which welding implementation is best and if we should use this one instead
	// Welded weights are averaged. TODO: add other welding options such as Max and Min
	static TArray<float> WeldWeightMap(const TConstArrayView<float>& UnweldedMap, const TArray<uint32>& PatternToWeldedIndices, const int32 NumWelded)
	{
		TArray<float> WeldedMap;
		WeldedMap.SetNumZeroed(NumWelded);

		TArray<float> NumInfluences;
		NumInfluences.SetNumZeroed(NumWelded);
		
		check(UnweldedMap.Num() == PatternToWeldedIndices.Num());
		for (int32 OrigIdx = 0; OrigIdx < UnweldedMap.Num(); ++OrigIdx)
		{
			const int32 WeldedIdx = PatternToWeldedIndices[OrigIdx];
			NumInfluences[WeldedIdx] += 1.f;
			WeldedMap[WeldedIdx] += UnweldedMap[OrigIdx];
		}

		for (int32 WeldedIdx = 0; WeldedIdx < NumWelded; ++WeldedIdx)
		{
			if (NumInfluences[WeldedIdx] > 0.f)
			{
				WeldedMap[WeldedIdx] /= NumInfluences[WeldedIdx];
			}
		}
		return WeldedMap;
	}

}  // End namespace UE::Chaos::ClothAsset

bool FChaosClothSimulationLodModel::Serialize(FArchive& Ar)
{
	// Serialize normal tagged property data
	if (Ar.IsLoading() || Ar.IsSaving())
	{
		UScriptStruct* const Struct = FChaosClothSimulationLodModel::StaticStruct();
		Struct->SerializeTaggedProperties(Ar, (uint8*)this, Struct, nullptr);
	}

	// Serialize weight maps (not a tagged property)
	Ar << WeightMaps;

	// Return true to confirm that serialization has already been taken care of
	return true;
}

FChaosClothSimulationModel::FChaosClothSimulationModel(const TSharedPtr<const FManagedArrayCollection>& ClothCollection, const FReferenceSkeleton& ReferenceSkeleton)
{
	using namespace UE::Chaos::ClothAsset;

	const FCollectionClothConstFacade Cloth(ClothCollection);

	// Retrieve weigh map names
	const TArray<FName> WeightMapNames = Cloth.GetWeightMapNames();

	// Initialize LOD models
	const int32 NumLods = Cloth.GetNumLods();
	ClothSimulationLodModels.SetNum(NumLods);

	for (int32 LodIndex = 0; LodIndex < NumLods; ++LodIndex)
	{
		FChaosClothSimulationLodModel& LodModel = ClothSimulationLodModels[LodIndex];
		const FCollectionClothLodConstFacade ClothLod = Cloth.GetLod(LodIndex);
		TArray<int32> WeldingMap;
		ClothLod.BuildSimulationMesh(LodModel.Positions, LodModel.Normals, LodModel.Indices, WeldingMap, LodModel.PatternPositions, LodModel.PatternIndices, LodModel.PatternToWeldedIndices);

		// Build welding index lookup  // TODO: Could probably do this better in the BuildSimulationMesh function
		const int32 LodModelNumSimVertices = LodModel.Positions.Num();
		const int32 ClothLodNumSimVertices = ClothLod.GetNumSimVertices();

		TArray<int32> ReverseWeldingMap;
		ReverseWeldingMap.Reserve(LodModelNumSimVertices);
		TMap<int32, TArray<int32>> SourceVertexIndicesMap;
		SourceVertexIndicesMap.Reserve(ClothLodNumSimVertices);
			
		for (int32 VertexIndex = 0; VertexIndex < ClothLodNumSimVertices; ++VertexIndex)
		{
			if (WeldingMap[VertexIndex] == VertexIndex)
			{
				SourceVertexIndicesMap.Emplace(VertexIndex, { VertexIndex } );
				ReverseWeldingMap.Emplace(VertexIndex);
			}
			else
			{
				TArray<int32>& SourceVertexIndices = SourceVertexIndicesMap.FindChecked(WeldingMap[VertexIndex]);
				SourceVertexIndices.Emplace(VertexIndex);
			}
		}

		// Copy and weld (average) weight maps
		LodModel.WeightMaps.Reserve(WeightMapNames.Num());

		for (const FName& WeightMapName : WeightMapNames)
		{
			const TConstArrayView<float> ClothLodWeightMap = ClothLod.GetWeightMap(WeightMapName);
			TArray<float>& LodModelWeightMap = LodModel.WeightMaps.Add(WeightMapName);

			LodModelWeightMap.SetNumZeroed(LodModelNumSimVertices);

			for (int32 WeldedIndex = 0; WeldedIndex < LodModelNumSimVertices; ++WeldedIndex)
			{
				const int32 BaseSourceVertexIndex = ReverseWeldingMap[WeldedIndex];
				const TArray<int32>& SourceVertexIndices = SourceVertexIndicesMap.FindChecked(BaseSourceVertexIndex);
				check(SourceVertexIndices.Num());

				for (const int32 VertexIndex : SourceVertexIndices)
				{
					LodModelWeightMap[WeldedIndex] += ClothLodWeightMap[VertexIndex];
				}

				LodModelWeightMap[WeldedIndex] /= (float)SourceVertexIndices.Num();
			}
		}

		// Weld bone influences
		TConstArrayView<int32> NumBoneInfluences = ClothLod.GetSimNumBoneInfluences();
		TConstArrayView<TArray<int32>> SimBoneIndices = ClothLod.GetSimBoneIndices();
		TConstArrayView<TArray<float>> SimBoneWeights = ClothLod.GetSimBoneWeights();
		LodModel.BoneData.SetNum(LodModelNumSimVertices);

		for (int32 WeldedIndex = 0; WeldedIndex < LodModelNumSimVertices; ++WeldedIndex)
		{
			const int32 SourceVertexIndex = ReverseWeldingMap[WeldedIndex];
			LodModel.BoneData[WeldedIndex].NumInfluences = NumBoneInfluences[SourceVertexIndex];
			for (int32 BoneIndex = 0; BoneIndex < LodModel.BoneData[WeldedIndex].NumInfluences; ++BoneIndex)
			{
				LodModel.BoneData[WeldedIndex].BoneIndices[BoneIndex] = SimBoneIndices[SourceVertexIndex][BoneIndex];
				LodModel.BoneData[WeldedIndex].BoneWeights[BoneIndex] = SimBoneWeights[SourceVertexIndex][BoneIndex];
			}
		}
	}

	// Populate used bone names and indices
	for (int32 Index = 0; Index < ReferenceSkeleton.GetRawBoneNum(); ++Index)
	{	
		UsedBoneNames.Add(ReferenceSkeleton.GetRawRefBoneInfo()[Index].Name);
		UsedBoneIndices.Add(Index);
	}

	// Initialize Reference bone index
	ReferenceBoneIndex = CalculateReferenceBoneIndex(ClothSimulationLodModels, ReferenceSkeleton, UsedBoneIndices);
}
