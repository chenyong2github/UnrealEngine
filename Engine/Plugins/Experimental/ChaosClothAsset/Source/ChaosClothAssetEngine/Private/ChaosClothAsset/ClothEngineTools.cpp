// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothEngineTools.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "ChaosClothAsset/ClothCollection.h"
#include "ChaosClothAsset/ClothGeometryTools.h"
#include "ClothTetherData.h"

namespace UE::Chaos::ClothAsset
{

void FClothEngineTools::GenerateTethers(const TSharedPtr<FManagedArrayCollection>& ClothCollection, const FName& WeightMap, const bool bGenerateGeodesicTethers)
{
	FCollectionClothFacade ClothFacade(ClothCollection);
	if (ClothFacade.HasWeightMap(WeightMap))
	{
		for (int32 LodIndex = 0; LodIndex < ClothFacade.GetNumLods(); ++LodIndex)
		{
			FCollectionClothLodFacade ClothLodFacade = ClothFacade.GetLod(LodIndex);

			// We want to create the tethers based on the welded mesh, so create that now
			TArray<FVector3f> Positions;
			TArray<FVector3f> Normals;
			TArray<uint32> Indices;
			TArray<FVector2f> PatternsPositions;
			TArray<uint32> PatternsIndices;
			TArray<uint32> PatternToWeldedIndices;
			TArray<TArray<int32>> WeldedToPatternIndices;
			ClothLodFacade.BuildSimulationMesh(Positions, Normals, Indices, PatternsPositions, PatternsIndices, PatternToWeldedIndices, &WeldedToPatternIndices);

			const TArray<float> WeldedWeightMap = FClothGeometryTools::BuildWeldedWeightMapForLod(ClothCollection, LodIndex, WeightMap, WeldedToPatternIndices);

			FClothTetherData TetherData;
			TetherData.GenerateTethers(TConstArrayView<FVector3f>(Positions), TConstArrayView<uint32>(Indices), TConstArrayView<float>(WeldedWeightMap), bGenerateGeodesicTethers);

			// Set new tethers
			const int32 NumTetherBatches = TetherData.Tethers.Num();
			ClothLodFacade.SetNumTetherBatches(NumTetherBatches);
			for (int32 TetherBatchIndex = 0; TetherBatchIndex < NumTetherBatches; ++TetherBatchIndex)
			{
				TArray<TTuple<int32, int32, float>>& TetherBatch = TetherData.Tethers[TetherBatchIndex];
				// Remap tether data indices back to unwelded indices. Just set them to the first of the unwelded index if that welded index is formed from multiple unwelded indices.
				for (TTuple<int32, int32, float>& Tether : TetherBatch)
				{
					Tether.Get<0>() = WeldedToPatternIndices[Tether.Get<0>()][0];
					Tether.Get<1>() = WeldedToPatternIndices[Tether.Get<1>()][0];
				}

				FCollectionClothTetherBatchFacade TetherBatchFacade = ClothLodFacade.GetTetherBatch(TetherBatchIndex);
				TetherBatchFacade.Initialize(TetherBatch);
			}
		}
	}
}

}  // End namespace UE::Chaos::ClothAsset
