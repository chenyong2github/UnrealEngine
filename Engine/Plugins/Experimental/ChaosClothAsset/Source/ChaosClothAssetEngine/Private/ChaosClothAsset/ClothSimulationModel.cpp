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
}  // End namespace UE::Chaos::ClothAsset

FChaosClothSimulationModel::FChaosClothSimulationModel(const TSharedPtr<const FManagedArrayCollection>& ClothCollection, const FReferenceSkeleton& ReferenceSkeleton)
{
	using namespace UE::Chaos::ClothAsset;

	const FCollectionClothConstFacade Cloth(ClothCollection);

	// Initialize LOD models
	const int32 NumLods = Cloth.GetNumLods();
	ClothSimulationLodModels.SetNum(NumLods);

	for (int32 LodIndex = 0; LodIndex < NumLods; ++LodIndex)
	{
		FChaosClothSimulationLodModel& LodModel = ClothSimulationLodModels[LodIndex];
		const FCollectionClothLodConstFacade ClothLod = Cloth.GetLod(LodIndex);
		TArray<int32> WeldingMap;
		ClothLod.BuildSimulationMesh(LodModel.Positions, LodModel.Normals, LodModel.Indices, WeldingMap);

		static const FName MaxDistanceName = TEXT("MaxDistance");
		const TConstArrayView<float> MaxDistanceValues = ClothLod.GetWeightMap(MaxDistanceName);

		if (MaxDistanceValues.Num())
		{
			LodModel.MaxDistance = MaxDistanceValues;
		}
		else
		{
			LodModel.MaxDistance.Init(1.0, LodModel.Positions.Num());
		}

		TConstArrayView<int32> NumBoneInfluences = ClothLod.GetSimNumBoneInfluences();
		TConstArrayView<TArray<int32>> SimBoneIndices = ClothLod.GetSimBoneIndices();
		TConstArrayView<TArray<float>> SimBoneWeights = ClothLod.GetSimBoneWeights();
		LodModel.BoneData.SetNum(LodModel.Positions.Num());

		uint32 WeldedIndex = 0;
		for (int32 VertexIndex = 0; VertexIndex <  ClothLod.GetNumSimVertices(); ++VertexIndex)
		{
			if (WeldingMap[VertexIndex] == VertexIndex)
			{
				LodModel.BoneData[WeldedIndex].NumInfluences = NumBoneInfluences[VertexIndex];
				for (int BoneIndex = 0; BoneIndex < LodModel.BoneData[WeldedIndex].NumInfluences; ++BoneIndex)
				{
					LodModel.BoneData[WeldedIndex].BoneIndices[BoneIndex] = SimBoneIndices[VertexIndex][BoneIndex];
					LodModel.BoneData[WeldedIndex].BoneWeights[BoneIndex] = SimBoneWeights[VertexIndex][BoneIndex];
				}
				++WeldedIndex;
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
