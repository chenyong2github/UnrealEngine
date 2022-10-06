// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothCollection.h"

namespace UE::Chaos::ClothAsset
{
	const FName UE::Chaos::ClothAsset::FClothCollection::SimVerticesGroup("SimVertices");
	const FName UE::Chaos::ClothAsset::FClothCollection::SimFacesGroup("SimFaces");
	const FName UE::Chaos::ClothAsset::FClothCollection::RenderVerticesGroup("RenderVertices");
	const FName UE::Chaos::ClothAsset::FClothCollection::RenderFacesGroup("RenderFaces");
	const FName UE::Chaos::ClothAsset::FClothCollection::WrapDeformersGroup("WrapDeformers");
	const FName UE::Chaos::ClothAsset::FClothCollection::PatternsGroup("Patterns");
	const FName UE::Chaos::ClothAsset::FClothCollection::StitchingsGroup("Stitchings");
	const FName UE::Chaos::ClothAsset::FClothCollection::TethersGroup("Tethers");
	const FName UE::Chaos::ClothAsset::FClothCollection::TetherBatchesGroup("TetherBatches");
	const FName UE::Chaos::ClothAsset::FClothCollection::LodsGroup("Lods");

	FClothCollection::FClothCollection()
	{
		Construct();
	}

	void FClothCollection::Serialize(FArchive& Ar)
	{
		::Chaos::FChaosArchive ChaosArchive(Ar);
		Super::Serialize(ChaosArchive);
	}

	void FClothCollection::Construct()
	{
		// Dependencies
		FManagedArrayCollection::FConstructionParameters SimVerticesDependency(SimVerticesGroup);
		FManagedArrayCollection::FConstructionParameters SimFacesDependency(SimFacesGroup);
		FManagedArrayCollection::FConstructionParameters RenderVerticesDependency(RenderVerticesGroup);
		FManagedArrayCollection::FConstructionParameters RenderFacesDependency(RenderFacesGroup);
		FManagedArrayCollection::FConstructionParameters WrapDeformersDependency(WrapDeformersGroup);
		FManagedArrayCollection::FConstructionParameters PatternsDependency(PatternsGroup);
		FManagedArrayCollection::FConstructionParameters StitchingsDependency(StitchingsGroup);
		FManagedArrayCollection::FConstructionParameters TethersDependency(TethersGroup);
		FManagedArrayCollection::FConstructionParameters TetherBatchesDependency(TetherBatchesGroup);

		// Sim Vertices Group
		AddExternalAttribute<FVector2f>("SimPosition", SimVerticesGroup, SimPosition);
		AddExternalAttribute<FVector3f>("SimRestPosition", SimVerticesGroup, SimRestPosition);
		AddExternalAttribute<FVector3f>("SimRestNormal", SimVerticesGroup, SimRestNormal);
		//AddExternalAttribute<FVertexBoneData>("SimBoneData", SimVerticesGroup, SimBoneData);

		// Sim Faces Group
		AddExternalAttribute<FIntVector3>("SimIndices", SimFacesGroup, SimIndices, SimVerticesDependency);

		// Render Vertices Group
		AddExternalAttribute<FVector3f>("RenderPosition", RenderVerticesGroup, RenderPosition);
		AddExternalAttribute<FVector3f>("RenderNormal", RenderVerticesGroup, RenderNormal);
		AddExternalAttribute<FVector3f>("RenderTangentU", RenderVerticesGroup, RenderTangentU);
		AddExternalAttribute<FVector3f>("RenderTangentV", RenderVerticesGroup, RenderTangentV);
		AddExternalAttribute<TArray<FVector2f>>("RenderUVs", RenderVerticesGroup, RenderUVs);
		AddExternalAttribute<FLinearColor>("RenderColor", RenderVerticesGroup, RenderColor);
		//AddExternalAttribute<FVertexBoneData>("RenderBoneData", RenderVerticesGroup, RenderBoneData);

		// Render Faces Group
		AddExternalAttribute<FIntVector3>("RenderIndices", RenderFacesGroup, RenderIndices, RenderVerticesDependency);
		AddExternalAttribute<int32>("RenderMaterialIndex", RenderFacesGroup, RenderMaterialIndex);

		// TODO: FMeshToMeshVertData

		// Patterns Group
		AddExternalAttribute<int32>("SimVerticesStart", PatternsGroup, SimVerticesStart, SimVerticesDependency);
		AddExternalAttribute<int32>("SimVerticesEnd", PatternsGroup, SimVerticesEnd, SimVerticesDependency);
		AddExternalAttribute<int32>("SimFacesStart", PatternsGroup, SimFacesStart, SimFacesDependency);
		AddExternalAttribute<int32>("SimFacesEnd", PatternsGroup, SimFacesEnd, SimFacesDependency);
		AddExternalAttribute<int32>("RenderVerticesStart", PatternsGroup, RenderVerticesStart, RenderVerticesDependency);
		AddExternalAttribute<int32>("RenderVerticesEnd", PatternsGroup, RenderVerticesEnd, RenderVerticesDependency);
		AddExternalAttribute<int32>("RenderFacesStart", PatternsGroup, RenderFacesStart, RenderFacesDependency);
		AddExternalAttribute<int32>("RenderFacesEnd", PatternsGroup, RenderFacesEnd, RenderFacesDependency);
		AddExternalAttribute<int32>("WrapDeformerStart", PatternsGroup, WrapDeformerStart, WrapDeformersDependency);
		AddExternalAttribute<int32>("WrapDeformerEnd", PatternsGroup, WrapDeformerEnd, WrapDeformersDependency);
		AddExternalAttribute<int32>("NumWeights", PatternsGroup, NumWeights);
		AddExternalAttribute<int32>("StatusFlags", PatternsGroup, StatusFlags);
		AddExternalAttribute<int32>("SimMaterialIndex", PatternsGroup, SimMaterialIndex);

		// Stitching Group
		AddExternalAttribute<TSet<int32>>("StitchedVertices", StitchingsGroup, StitchedVertices, SimVerticesDependency);

		// Tethers Group
		AddExternalAttribute<int32>("TetherKinematicIndex", TethersGroup, TetherKinematicIndex, SimVerticesDependency);
		AddExternalAttribute<int32>("TetherDynamicIndex", TethersGroup, TetherDynamicIndex, SimVerticesDependency);
		AddExternalAttribute<float>("TetherReferenceLength", TethersGroup, TetherReferenceLength);

		// Tether Batches Group
		AddExternalAttribute<int32>("TetherStart", TetherBatchesGroup, TetherStart, TethersDependency);
		AddExternalAttribute<int32>("TetherEnd", TetherBatchesGroup, TetherEnd, TethersDependency);

		// LOD Group
		AddExternalAttribute<int32>("PatternStart", LodsGroup, PatternStart, PatternsDependency);
		AddExternalAttribute<int32>("PatternEnd", LodsGroup, PatternEnd, PatternsDependency);
		AddExternalAttribute<int32>("StitchingStart", LodsGroup, StitchingStart, StitchingsDependency);
		AddExternalAttribute<int32>("StitchingEnd", LodsGroup, StitchingEnd, StitchingsDependency);
		AddExternalAttribute<int32>("TetherBatchStart", LodsGroup, TetherBatchStart, TetherBatchesDependency);
		AddExternalAttribute<int32>("TetherBatchEnd", LodsGroup, TetherBatchEnd, TetherBatchesDependency);
		AddExternalAttribute<int32>("LodBiasDepth", LodsGroup, LodBiasDepth);
	}
} // End namespace UE::Chaos::ClothAsset
