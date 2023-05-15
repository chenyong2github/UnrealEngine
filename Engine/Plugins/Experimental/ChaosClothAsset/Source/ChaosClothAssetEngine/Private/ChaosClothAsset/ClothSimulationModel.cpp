// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothSimulationModel.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "ChaosClothAsset/ClothGeometryTools.h"
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

		TArray<TArray<int32>> WeldedToPatternIndices;
		ClothLod.BuildSimulationMesh(LodModel.Positions, LodModel.Normals, LodModel.Indices, LodModel.PatternPositions, LodModel.PatternIndices, LodModel.PatternToWeldedIndices, &WeldedToPatternIndices);

		const int32 LodModelNumSimVertices = LodModel.Positions.Num();
		const int32 ClothLodNumSimVertices = ClothLod.GetNumSimVertices();

		// Copy and weld (average) weight maps
		LodModel.WeightMaps.Reserve(WeightMapNames.Num());

		for (const FName& WeightMapName : WeightMapNames)
		{
			const TConstArrayView<float> ClothLodWeightMap = ClothLod.GetWeightMap(WeightMapName);
			TArray<float>& LodModelWeightMap = LodModel.WeightMaps.Add(WeightMapName);

			LodModelWeightMap = FClothGeometryTools::BuildWeldedWeightMapForLod(ClothCollection, LodIndex, WeightMapName, WeldedToPatternIndices);
		}

		// Weld bone influences
		TConstArrayView<int32> NumBoneInfluences = ClothLod.GetSimNumBoneInfluences();
		TConstArrayView<TArray<int32>> SimBoneIndices = ClothLod.GetSimBoneIndices();
		TConstArrayView<TArray<float>> SimBoneWeights = ClothLod.GetSimBoneWeights();
		LodModel.BoneData.SetNum(LodModelNumSimVertices);

		for (int32 WeldedIndex = 0; WeldedIndex < LodModelNumSimVertices; ++WeldedIndex)
		{
			const TArray<int32>& SourceVertices = WeldedToPatternIndices[WeldedIndex];
			check(SourceVertices.Num());
			const int32 SourceVertexIndex = SourceVertices[0]; // Just using first bone's data.

			LodModel.BoneData[WeldedIndex].NumInfluences = NumBoneInfluences[SourceVertexIndex];
			for (int32 BoneIndex = 0; BoneIndex < LodModel.BoneData[WeldedIndex].NumInfluences; ++BoneIndex)
			{
				LodModel.BoneData[WeldedIndex].BoneIndices[BoneIndex] = SimBoneIndices[SourceVertexIndex][BoneIndex];
				LodModel.BoneData[WeldedIndex].BoneWeights[BoneIndex] = SimBoneWeights[SourceVertexIndex][BoneIndex];
			}
		}

		// Weld tethers. This will not deduplicate any tethers that may be duplicated by welding. 
		// This would currently only occur if seams were added after tethers were created in the dataflow graph.
		const int32 NumTetherBatches = ClothLod.GetNumTetherBatches();
		LodModel.TetherData.Tethers.Reserve(NumTetherBatches);
		for (int32 TetherBatchIndex = 0; TetherBatchIndex < NumTetherBatches; ++TetherBatchIndex)
		{
			FCollectionClothTetherBatchConstFacade TetherBatchFacade = ClothLod.GetTetherBatch(TetherBatchIndex);
			TArray<TTuple<int32, int32, float>>& Tethers = LodModel.TetherData.Tethers.Emplace_GetRef(TetherBatchFacade.GetZippedTetherData());

			for (TTuple<int32, int32, float>& Tether : Tethers)
			{
				Tether.Get<0>() = LodModel.PatternToWeldedIndices[Tether.Get<0>()];
				Tether.Get<1>() = LodModel.PatternToWeldedIndices[Tether.Get<1>()];
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

TArray<TConstArrayView<TTuple<int32, int32, float>>> FChaosClothSimulationModel::GetTethers(int32 LODIndex) const
{
	TArray<TConstArrayView<TTuple<int32, int32, float>>> Tethers;
	if (IsValidLodIndex(LODIndex))
	{
		const FClothTetherData& ClothTetherData = ClothSimulationLodModels[LODIndex].TetherData;

		const int32 NumTetherBatches = ClothTetherData.Tethers.Num();
		Tethers.Reserve(NumTetherBatches);
		for (int32 Index = 0; Index < NumTetherBatches; ++Index)
		{
			Tethers.Emplace(TConstArrayView<TTuple<int32, int32, float>>(ClothTetherData.Tethers[Index]));
		}
	}
	return Tethers;
}