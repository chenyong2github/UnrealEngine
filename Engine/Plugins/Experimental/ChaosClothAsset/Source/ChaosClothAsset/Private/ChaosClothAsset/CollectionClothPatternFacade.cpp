// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/CollectionClothPatternFacade.h"
#include "ChaosClothAsset/ClothCollection.h"

namespace UE::Chaos::ClothAsset
{
	int32 FCollectionClothPatternConstFacade::GetStatusFlags() const
	{
		return (*ClothCollection->GetStatusFlags())[GetElementIndex()];
	}

	int32 FCollectionClothPatternConstFacade::GetNumSimVertices() const
	{
		return ClothCollection->GetNumElements(
			ClothCollection->GetSimVerticesStart(),
			ClothCollection->GetSimVerticesEnd(),
			GetElementIndex());
	}

	int32 FCollectionClothPatternConstFacade::GetNumSimFaces() const
	{
		return ClothCollection->GetNumElements(
			ClothCollection->GetSimFacesStart(),
			ClothCollection->GetSimFacesEnd(),
			GetElementIndex());
	}

	int32 FCollectionClothPatternConstFacade::GetNumRenderVertices() const
	{
		return ClothCollection->GetNumElements(
			ClothCollection->GetRenderVerticesStart(),
			ClothCollection->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	int32 FCollectionClothPatternConstFacade::GetNumRenderFaces() const
	{
		return ClothCollection->GetNumElements(
			ClothCollection->GetRenderFacesStart(),
			ClothCollection->GetRenderFacesEnd(),
			GetElementIndex());
	}

	int32 FCollectionClothPatternConstFacade::GetSimVerticesOffset() const
	{
		return ClothCollection->GetElementsOffset(
			ClothCollection->GetSimVerticesStart(),
			GetBaseElementIndex(),
			GetElementIndex());
	}

	int32 FCollectionClothPatternConstFacade::GetSimFacesOffset() const
	{
		return ClothCollection->GetElementsOffset(
			ClothCollection->GetSimFacesStart(),
			GetBaseElementIndex(),
			GetElementIndex());
	}

	int32 FCollectionClothPatternConstFacade::GetRenderVerticesOffset() const
	{
		return ClothCollection->GetElementsOffset(
			ClothCollection->GetRenderVerticesStart(),
			GetBaseElementIndex(),
			GetElementIndex());
	}

	int32 FCollectionClothPatternConstFacade::GetRenderFacesOffset() const
	{
		return ClothCollection->GetElementsOffset(
			ClothCollection->GetRenderFacesStart(),
			GetBaseElementIndex(),
			GetElementIndex());
	}

	TConstArrayView<FVector2f> FCollectionClothPatternConstFacade::GetSimPosition() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetSimPosition(),
			ClothCollection->GetSimVerticesStart(),
			ClothCollection->GetSimVerticesEnd(),
			GetElementIndex());
	}

	TConstArrayView<FVector3f> FCollectionClothPatternConstFacade::GetSimRestPosition() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetSimRestPosition(),
			ClothCollection->GetSimVerticesStart(),
			ClothCollection->GetSimVerticesEnd(),
			GetElementIndex());
	}

	TConstArrayView<FVector3f> FCollectionClothPatternConstFacade::GetSimRestNormal() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetSimRestNormal(),
			ClothCollection->GetSimVerticesStart(),
			ClothCollection->GetSimVerticesEnd(),
			GetElementIndex());
	}

	TConstArrayView<int32> FCollectionClothPatternConstFacade::GetSimNumBoneInfluences() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetSimNumBoneInfluences(),
			ClothCollection->GetSimVerticesStart(),
			ClothCollection->GetSimVerticesEnd(),
			GetElementIndex());
	}

	TConstArrayView<TArray<int32>> FCollectionClothPatternConstFacade::GetSimBoneIndices() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetSimBoneIndices(),
			ClothCollection->GetSimVerticesStart(),
			ClothCollection->GetSimVerticesEnd(),
			GetElementIndex());
	}

	TConstArrayView<TArray<float>> FCollectionClothPatternConstFacade::GetSimBoneWeights() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetSimBoneWeights(),
			ClothCollection->GetSimVerticesStart(),
			ClothCollection->GetSimVerticesEnd(),
			GetElementIndex());
	}

	TConstArrayView<FIntVector3> FCollectionClothPatternConstFacade::GetSimIndices() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetSimIndices(),
			ClothCollection->GetSimFacesStart(),
			ClothCollection->GetSimFacesEnd(),
			GetElementIndex());
	}

	TConstArrayView<FVector3f> FCollectionClothPatternConstFacade::GetRenderPosition() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetRenderPosition(),
			ClothCollection->GetRenderVerticesStart(),
			ClothCollection->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TConstArrayView<FVector3f> FCollectionClothPatternConstFacade::GetRenderNormal() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetRenderNormal(),
			ClothCollection->GetRenderVerticesStart(),
			ClothCollection->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TConstArrayView<FVector3f> FCollectionClothPatternConstFacade::GetRenderTangentU() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetRenderTangentU(),
			ClothCollection->GetRenderVerticesStart(),
			ClothCollection->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TConstArrayView<FVector3f> FCollectionClothPatternConstFacade::GetRenderTangentV() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetRenderTangentV(),
			ClothCollection->GetRenderVerticesStart(),
			ClothCollection->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TConstArrayView<TArray<FVector2f>> FCollectionClothPatternConstFacade::GetRenderUVs() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetRenderUVs(),
			ClothCollection->GetRenderVerticesStart(),
			ClothCollection->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TConstArrayView<FLinearColor> FCollectionClothPatternConstFacade::GetRenderColor() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetRenderColor(),
			ClothCollection->GetRenderVerticesStart(),
			ClothCollection->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TConstArrayView<int32> FCollectionClothPatternConstFacade::GetRenderNumBoneInfluences() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetRenderNumBoneInfluences(),
			ClothCollection->GetRenderVerticesStart(),
			ClothCollection->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TConstArrayView<TArray<int32>> FCollectionClothPatternConstFacade::GetRenderBoneIndices() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetRenderBoneIndices(),
			ClothCollection->GetRenderVerticesStart(),
			ClothCollection->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TConstArrayView<TArray<float>> FCollectionClothPatternConstFacade::GetRenderBoneWeights() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetRenderBoneWeights(),
			ClothCollection->GetRenderVerticesStart(),
			ClothCollection->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TConstArrayView<FIntVector3> FCollectionClothPatternConstFacade::GetRenderIndices() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetRenderIndices(),
			ClothCollection->GetRenderFacesStart(),
			ClothCollection->GetRenderFacesEnd(),
			GetElementIndex());
	}

	TConstArrayView<int32> FCollectionClothPatternConstFacade::GetRenderMaterialIndex() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetRenderMaterialIndex(),
			ClothCollection->GetRenderFacesStart(),
			ClothCollection->GetRenderFacesEnd(),
			GetElementIndex());
	}

	TConstArrayView<float> FCollectionClothPatternConstFacade::GetWeightMap(const FName& Name) const
	{
		const TManagedArray<float>* const WeightMap = ClothCollection->GetUserDefinedAttribute<float>(Name, FClothCollection::SimVerticesGroup);
		return ClothCollection->GetElements(
			WeightMap,
			ClothCollection->GetSimVerticesStart(),
			ClothCollection->GetSimVerticesEnd(),
			GetElementIndex());
	}

	FCollectionClothPatternConstFacade::FCollectionClothPatternConstFacade(const TSharedPtr<const FClothCollection>& ClothCollection, int32 InLodIndex, int32 InPatternIndex)
		: ClothCollection(ClothCollection)
		, LodIndex(InLodIndex)
		, PatternIndex(InPatternIndex)
	{
		check(ClothCollection.IsValid());
		check(ClothCollection->IsValid());
		check(LodIndex >= 0 && LodIndex < ClothCollection->GetNumElements(FClothCollection::LodsGroup));
		check(PatternIndex >= 0 && PatternIndex < ClothCollection->GetNumElements(ClothCollection->GetPatternStart(), ClothCollection->GetPatternEnd(), LodIndex));
	}

	int32 FCollectionClothPatternConstFacade::GetBaseElementIndex() const
	{
		return (*ClothCollection->GetPatternStart())[LodIndex];
	}

	void FCollectionClothPatternFacade::Reset()
	{
		SetNumSimVertices(0);
		SetNumSimFaces(0);
		SetNumRenderVertices(0);
		SetNumRenderFaces(0);
		SetDefaults();
	}

	void FCollectionClothPatternFacade::Initialize(const TArray<FVector2f>& Positions, const TArray<FVector3f>& RestPositions, const TArray<uint32>& Indices)
	{
		Reset();

		const int32 NumSimVertices = Positions.Num();
		check(NumSimVertices == RestPositions.Num());

		SetNumSimVertices(NumSimVertices);

		const TArrayView<FVector2f> SimPosition = GetSimPosition();
		const TArrayView<FVector3f> SimRestPosition = GetSimRestPosition();
		const TArrayView<FVector3f> SimRestNormal = GetSimRestNormal();

		for (int32 SimVertexIndex = 0; SimVertexIndex < NumSimVertices; ++SimVertexIndex)
		{
			SimPosition[SimVertexIndex] = Positions[SimVertexIndex];
			SimRestPosition[SimVertexIndex] = RestPositions[SimVertexIndex];
			SimRestNormal[SimVertexIndex] = FVector3f::ZeroVector;
		}

		const int32 NumSimFaces = (uint32)Indices.Num() / 3;
		check(NumSimFaces * 3 == Indices.Num());

		SetNumSimFaces(NumSimFaces);

		const TArrayView<FIntVector3> SimIndices = GetSimIndices();

		// Face indices always index from the first vertex of the LOD, but these indices are indexed from the start of the pattern and need to be offset
		const int32 LodSimVerticesOffset = ClothCollection->GetElementsOffset(
			ClothCollection->GetSimVerticesStart(),
			GetBaseElementIndex(),
			GetElementIndex());

		for (int32 SimFaceIndex = 0; SimFaceIndex < NumSimFaces; ++SimFaceIndex)
		{
			// Indices from the start of the pattern
			const int32 PattternVertexIndex0 = Indices[SimFaceIndex * 3];
			const int32 PattternVertexIndex1 = Indices[SimFaceIndex * 3 + 1];
			const int32 PattternVertexIndex2 = Indices[SimFaceIndex * 3 + 2];

			// Indices from the start of the LOD
			const int32 LodVertexIndex0 = PattternVertexIndex0 + LodSimVerticesOffset;
			const int32 LodVertexIndex1 = PattternVertexIndex1 + LodSimVerticesOffset;
			const int32 LodVertexIndex2 = PattternVertexIndex2 + LodSimVerticesOffset;

			// Set indices in LOD index space
			SimIndices[SimFaceIndex] = FIntVector3(LodVertexIndex0, LodVertexIndex1, LodVertexIndex2);

			// Calculate face normal contribution
			const FVector3f& Pos0 = SimRestPosition[PattternVertexIndex0];
			const FVector3f& Pos1 = SimRestPosition[PattternVertexIndex1];
			const FVector3f& Pos2 = SimRestPosition[PattternVertexIndex2];
			const FVector3f Normal = (Pos1 - Pos0).Cross(Pos2 - Pos0).GetSafeNormal();
			SimRestNormal[PattternVertexIndex0] += Normal;
			SimRestNormal[PattternVertexIndex1] += Normal;
			SimRestNormal[PattternVertexIndex2] += Normal;
		}

		// Normalize normals
		for (int32 SimVertexIndex = 0; SimVertexIndex < NumSimVertices; ++SimVertexIndex)
		{
			SimRestNormal[SimVertexIndex] = SimRestNormal[SimVertexIndex].GetSafeNormal(UE_SMALL_NUMBER, FVector3f::XAxisVector);
		}
	}

	void FCollectionClothPatternFacade::Initialize(const FCollectionClothPatternConstFacade& Other)
	{
		Reset();

		// Patterns Group
		SetStatusFlags(Other.GetStatusFlags());

		// Sim Vertices Group
		const int32 NumSimVertices = Other.GetNumSimVertices();
		SetNumSimVertices(NumSimVertices);
		
		const TConstArrayView<FVector2f> OtherSimPosition = Other.GetSimPosition();
		const TConstArrayView<FVector3f> OtherSimRestPosition = Other.GetSimRestPosition();
		const TConstArrayView<FVector3f> OtherSimRestNormal = Other.GetSimRestNormal();
		const TConstArrayView<int32> OtherSimNumBoneInfluences = Other.GetSimNumBoneInfluences();
		const TConstArrayView<TArray<int32>> OtherSimBoneIndices = Other.GetSimBoneIndices();
		const TConstArrayView<TArray<float>> OtherSimBoneWeights = Other.GetSimBoneWeights();
		const TArrayView<FVector2f> SimPosition = GetSimPosition();
		const TArrayView<FVector3f> SimRestPosition = GetSimRestPosition();
		const TArrayView<FVector3f> SimRestNormal = GetSimRestNormal();
		const TArrayView<int32> SimNumBoneInfluences = GetSimNumBoneInfluences();
		const TArrayView<TArray<int32>> SimBoneIndices = GetSimBoneIndices();
		const TArrayView<TArray<float>> SimBoneWeights = GetSimBoneWeights();

		for (int32 SimVertexIndex = 0; SimVertexIndex < NumSimVertices; ++SimVertexIndex)
		{
			SimPosition[SimVertexIndex] = Other.GetSimPosition()[SimVertexIndex];
			SimRestPosition[SimVertexIndex] = Other.GetSimRestPosition()[SimVertexIndex];
			SimRestNormal[SimVertexIndex] = Other.GetSimRestNormal()[SimVertexIndex];
			SimNumBoneInfluences[SimVertexIndex] = Other.GetSimNumBoneInfluences()[SimVertexIndex];
			SimBoneIndices[SimVertexIndex] = Other.GetSimBoneIndices()[SimVertexIndex];
			SimBoneWeights[SimVertexIndex] = Other.GetSimBoneWeights()[SimVertexIndex];
		}

		// Sim Faces Group
		const int32 NumSimFaces = Other.GetNumSimFaces();
		SetNumSimFaces(NumSimFaces);

		const int32 LodSimVerticesOffset = ClothCollection->GetElementsOffset(
			ClothCollection->GetSimVerticesStart(),
			GetBaseElementIndex(),
			GetElementIndex());
		const int32 OtherLodSimVerticesOffset = Other.ClothCollection->GetElementsOffset(
			ClothCollection->GetSimVerticesStart(),
			GetBaseElementIndex(),
			GetElementIndex());
		const FIntVector3 OtherToLodSimVerticesOffset(LodSimVerticesOffset - OtherLodSimVerticesOffset);

		const TConstArrayView<FIntVector3> OtherSimIndices = Other.GetSimIndices();
		const TArrayView<FIntVector3> SimIndices = GetSimIndices();
		
		for (int32 SimFaceIndex = 0; SimFaceIndex < NumSimFaces; ++SimFaceIndex)
		{
			SimIndices[SimFaceIndex] = OtherSimIndices[SimFaceIndex] + OtherToLodSimVerticesOffset;
		}

		// Render Vertices Group
		const int32 NumRenderVertices = Other.GetNumRenderVertices();
		SetNumRenderVertices(NumRenderVertices);

		const TConstArrayView<FVector3f> OtherRenderPosition = Other.GetRenderPosition();
		const TConstArrayView<FVector3f> OtherRenderNormal = Other.GetRenderNormal();
		const TConstArrayView<FVector3f> OtherRenderTangentU = Other.GetRenderTangentU();
		const TConstArrayView<FVector3f> OtherRenderTangentV = Other.GetRenderTangentV();
		const TConstArrayView<TArray<FVector2f>> OtherRenderUVs = Other.GetRenderUVs();
		const TConstArrayView<FLinearColor> OtherRenderColor = Other.GetRenderColor();
		const TConstArrayView<int32> OtherRenderNumBoneInfluences = Other.GetRenderNumBoneInfluences();
		const TConstArrayView<TArray<int32>> OtherRenderBoneIndices = Other.GetRenderBoneIndices();
		const TConstArrayView<TArray<float>> OtherRenderBoneWeights = Other.GetRenderBoneWeights();
		const TArrayView<FVector3f> RenderPosition = GetRenderPosition();
		const TArrayView<FVector3f> RenderNormal = GetRenderNormal();
		const TArrayView<FVector3f> RenderTangentU = GetRenderTangentU();
		const TArrayView<FVector3f> RenderTangentV = GetRenderTangentV();
		const TArrayView<TArray<FVector2f>> RenderUVs = GetRenderUVs();
		const TArrayView<FLinearColor> RenderColor = GetRenderColor();
		const TArrayView<int32> RenderNumBoneInfluences = GetRenderNumBoneInfluences();
		const TArrayView<TArray<int32>> RenderBoneIndices = GetRenderBoneIndices();
		const TArrayView<TArray<float>> RenderBoneWeights = GetRenderBoneWeights();

		for (int32 RenderVertexIndex = 0; RenderVertexIndex < NumRenderVertices; ++RenderVertexIndex)
		{
			RenderPosition[RenderVertexIndex] = OtherRenderPosition[RenderVertexIndex];
			RenderNormal[RenderVertexIndex] = OtherRenderNormal[RenderVertexIndex];
			RenderTangentU[RenderVertexIndex] = OtherRenderTangentU[RenderVertexIndex];
			RenderTangentV[RenderVertexIndex] = OtherRenderTangentV[RenderVertexIndex];
			RenderUVs[RenderVertexIndex] = OtherRenderUVs[RenderVertexIndex];
			RenderColor[RenderVertexIndex] = OtherRenderColor[RenderVertexIndex];
			RenderNumBoneInfluences[RenderVertexIndex] = OtherRenderNumBoneInfluences[RenderVertexIndex];
			RenderBoneIndices[RenderVertexIndex] = OtherRenderBoneIndices[RenderVertexIndex];
			RenderBoneWeights[RenderVertexIndex] = OtherRenderBoneWeights[RenderVertexIndex];
		}

		// Render Faces Group
		const int32 NumRenderFaces = Other.GetNumRenderFaces();
		SetNumRenderFaces(NumRenderFaces);

		const int32 LodRenderVerticesOffset = ClothCollection->GetElementsOffset(
			ClothCollection->GetRenderVerticesStart(),
			GetBaseElementIndex(),
			GetElementIndex());
		const int32 OtherLodRenderVerticesOffset = Other.ClothCollection->GetElementsOffset(
			ClothCollection->GetRenderVerticesStart(),
			GetBaseElementIndex(),
			GetElementIndex());

		const FIntVector3 OtherToLodRenderVerticesOffset(LodRenderVerticesOffset - OtherLodRenderVerticesOffset);

		for (int32 RenderFaceIndex = 0; RenderFaceIndex < NumRenderFaces; ++RenderFaceIndex)
		{
			GetRenderIndices()[RenderFaceIndex] = Other.GetRenderIndices()[RenderFaceIndex] + OtherToLodRenderVerticesOffset;
			GetRenderMaterialIndex()[RenderFaceIndex] = Other.GetRenderMaterialIndex()[RenderFaceIndex];
		}
	}

	void FCollectionClothPatternFacade::SetNumSimVertices(int32 NumSimVertices)
	{
		GetClothCollection()->SetNumElements(
			NumSimVertices,
			FClothCollection::SimVerticesGroup,
			GetClothCollection()->GetSimVerticesStart(),
			GetClothCollection()->GetSimVerticesEnd(),
			GetElementIndex());
	}

	void FCollectionClothPatternFacade::SetNumSimFaces(int32 NumSimFaces)
	{
		GetClothCollection()->SetNumElements(
			NumSimFaces,
			FClothCollection::SimFacesGroup,
			GetClothCollection()->GetSimFacesStart(),
			GetClothCollection()->GetSimFacesEnd(),
			GetElementIndex());
	}

	void FCollectionClothPatternFacade::SetNumRenderVertices(int32 NumRenderVertices)
	{
		GetClothCollection()->SetNumElements(
			NumRenderVertices,
			FClothCollection::RenderVerticesGroup,
			GetClothCollection()->GetRenderVerticesStart(),
			GetClothCollection()->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	void FCollectionClothPatternFacade::SetNumRenderFaces(int32 NumRenderFaces)
	{
		GetClothCollection()->SetNumElements(
			NumRenderFaces,
			FClothCollection::RenderFacesGroup,
			GetClothCollection()->GetRenderFacesStart(),
			GetClothCollection()->GetRenderFacesEnd(),
			GetElementIndex());
	}

	void FCollectionClothPatternFacade::SetStatusFlags(int32 StatusFlags)
	{
		(*GetClothCollection()->GetStatusFlags())[GetElementIndex()] = StatusFlags;
	}

	TArrayView<FVector2f> FCollectionClothPatternFacade::GetSimPosition()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetSimPosition(),
			GetClothCollection()->GetSimVerticesStart(),
			GetClothCollection()->GetSimVerticesEnd(),
			GetElementIndex());
	}

	TArrayView<FVector3f> FCollectionClothPatternFacade::GetSimRestPosition()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetSimRestPosition(),
			GetClothCollection()->GetSimVerticesStart(),
			GetClothCollection()->GetSimVerticesEnd(),
			GetElementIndex());
	}

	TArrayView<FVector3f> FCollectionClothPatternFacade::GetSimRestNormal()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetSimRestNormal(),
			GetClothCollection()->GetSimVerticesStart(),
			GetClothCollection()->GetSimVerticesEnd(),
			GetElementIndex());
	}

	TArrayView<int32> FCollectionClothPatternFacade::GetSimNumBoneInfluences()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetSimNumBoneInfluences(),
			GetClothCollection()->GetSimVerticesStart(),
			GetClothCollection()->GetSimVerticesEnd(),
			GetElementIndex());
	}

	TArrayView<TArray<int32>> FCollectionClothPatternFacade::GetSimBoneIndices()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetSimBoneIndices(),
			GetClothCollection()->GetSimVerticesStart(),
			GetClothCollection()->GetSimVerticesEnd(),
			GetElementIndex());
	}

	TArrayView<TArray<float>> FCollectionClothPatternFacade::GetSimBoneWeights()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetSimBoneWeights(),
			GetClothCollection()->GetSimVerticesStart(),
			GetClothCollection()->GetSimVerticesEnd(),
			GetElementIndex());
	}

	TArrayView<FIntVector3> FCollectionClothPatternFacade::GetSimIndices()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetSimIndices(),
			GetClothCollection()->GetSimFacesStart(),
			GetClothCollection()->GetSimFacesEnd(),
			GetElementIndex());
	}

	TArrayView<FVector3f> FCollectionClothPatternFacade::GetRenderPosition()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetRenderPosition(),
			GetClothCollection()->GetRenderVerticesStart(),
			GetClothCollection()->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TArrayView<FVector3f> FCollectionClothPatternFacade::GetRenderNormal()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetRenderNormal(),
			GetClothCollection()->GetRenderVerticesStart(),
			GetClothCollection()->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TArrayView<FVector3f> FCollectionClothPatternFacade::GetRenderTangentU()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetRenderTangentU(),
			GetClothCollection()->GetRenderVerticesStart(),
			GetClothCollection()->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TArrayView<FVector3f> FCollectionClothPatternFacade::GetRenderTangentV()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetRenderTangentV(),
			GetClothCollection()->GetRenderVerticesStart(),
			GetClothCollection()->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TArrayView<TArray<FVector2f>> FCollectionClothPatternFacade::GetRenderUVs()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetRenderUVs(),
			GetClothCollection()->GetRenderVerticesStart(),
			GetClothCollection()->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TArrayView<FLinearColor> FCollectionClothPatternFacade::GetRenderColor()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetRenderColor(),
			GetClothCollection()->GetRenderVerticesStart(),
			GetClothCollection()->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TArrayView<int32> FCollectionClothPatternFacade::GetRenderNumBoneInfluences()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetRenderNumBoneInfluences(),
			GetClothCollection()->GetRenderVerticesStart(),
			GetClothCollection()->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TArrayView<TArray<int32>> FCollectionClothPatternFacade::GetRenderBoneIndices()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetRenderBoneIndices(),
			GetClothCollection()->GetRenderVerticesStart(),
			GetClothCollection()->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TArrayView<TArray<float>> FCollectionClothPatternFacade::GetRenderBoneWeights()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetRenderBoneWeights(),
			GetClothCollection()->GetRenderVerticesStart(),
			GetClothCollection()->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TArrayView<FIntVector3> FCollectionClothPatternFacade::GetRenderIndices()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetRenderIndices(),
			GetClothCollection()->GetRenderFacesStart(),
			GetClothCollection()->GetRenderFacesEnd(),
			GetElementIndex());
	}

	TArrayView<int32> FCollectionClothPatternFacade::GetRenderMaterialIndex()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetRenderMaterialIndex(),
			GetClothCollection()->GetRenderFacesStart(),
			GetClothCollection()->GetRenderFacesEnd(),
			GetElementIndex());
	}

	TArrayView<float> FCollectionClothPatternFacade::GetWeightMap(const FName& Name)
	{
		TManagedArray<float>* const WeightMap = GetClothCollection()->GetUserDefinedAttribute<float>(Name, FClothCollection::SimVerticesGroup);
		return GetClothCollection()->GetElements(
			WeightMap,
			GetClothCollection()->GetSimVerticesStart(),
			GetClothCollection()->GetSimVerticesEnd(),
			GetElementIndex());
	}

	FCollectionClothPatternFacade::FCollectionClothPatternFacade(const TSharedPtr<FClothCollection>& ClothCollection, int32 InLodIndex, int32 InPatternIndex)
		: FCollectionClothPatternConstFacade(ClothCollection, InLodIndex, InPatternIndex)
	{
	}

	void FCollectionClothPatternFacade::SetDefaults()
	{
		const int32 ElementIndex = GetElementIndex();

		(*GetClothCollection()->GetSimVerticesStart())[ElementIndex] = INDEX_NONE;
		(*GetClothCollection()->GetSimVerticesEnd())[ElementIndex] = INDEX_NONE;
		(*GetClothCollection()->GetSimFacesStart())[ElementIndex] = INDEX_NONE;
		(*GetClothCollection()->GetSimFacesEnd())[ElementIndex] = INDEX_NONE;
		(*GetClothCollection()->GetRenderVerticesStart())[ElementIndex] = INDEX_NONE;
		(*GetClothCollection()->GetRenderVerticesEnd())[ElementIndex] = INDEX_NONE;
		(*GetClothCollection()->GetRenderFacesStart())[ElementIndex] = INDEX_NONE;
		(*GetClothCollection()->GetRenderFacesEnd())[ElementIndex] = INDEX_NONE;
		(*GetClothCollection()->GetStatusFlags())[ElementIndex] = 0;
	}
}  // End namespace UE::Chaos::ClothAsset
