// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/CollectionClothRenderPatternFacade.h"
#include "ChaosClothAsset/ClothCollection.h"
#include "ChaosClothAsset/ClothGeometryTools.h"

namespace UE::Chaos::ClothAsset
{
	const FString& FCollectionClothRenderPatternConstFacade::GetRenderMaterialPathName() const
	{
		static const FString EmptyString;
		return ClothCollection->GetRenderMaterialPathName() && ClothCollection->GetNumElements(FClothCollection::RenderPatternsGroup) > GetElementIndex() ? 
			(*ClothCollection->GetRenderMaterialPathName())[GetElementIndex()] : EmptyString;
	}

	int32 FCollectionClothRenderPatternConstFacade::GetNumRenderVertices() const
	{
		return ClothCollection->GetNumElements(
			ClothCollection->GetRenderVerticesStart(),
			ClothCollection->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	int32 FCollectionClothRenderPatternConstFacade::GetRenderVerticesOffset() const
	{
		return ClothCollection->GetElementsOffset(
			ClothCollection->GetRenderVerticesStart(),
			GetBaseElementIndex(),
			GetElementIndex());
	}

	TConstArrayView<FVector3f> FCollectionClothRenderPatternConstFacade::GetRenderPosition() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetRenderPosition(),
			ClothCollection->GetRenderVerticesStart(),
			ClothCollection->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TConstArrayView<FVector3f> FCollectionClothRenderPatternConstFacade::GetRenderNormal() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetRenderNormal(),
			ClothCollection->GetRenderVerticesStart(),
			ClothCollection->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TConstArrayView<FVector3f> FCollectionClothRenderPatternConstFacade::GetRenderTangentU() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetRenderTangentU(),
			ClothCollection->GetRenderVerticesStart(),
			ClothCollection->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TConstArrayView<FVector3f> FCollectionClothRenderPatternConstFacade::GetRenderTangentV() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetRenderTangentV(),
			ClothCollection->GetRenderVerticesStart(),
			ClothCollection->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TConstArrayView<TArray<FVector2f>> FCollectionClothRenderPatternConstFacade::GetRenderUVs() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetRenderUVs(),
			ClothCollection->GetRenderVerticesStart(),
			ClothCollection->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TConstArrayView<FLinearColor> FCollectionClothRenderPatternConstFacade::GetRenderColor() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetRenderColor(),
			ClothCollection->GetRenderVerticesStart(),
			ClothCollection->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TConstArrayView<TArray<int32>> FCollectionClothRenderPatternConstFacade::GetRenderBoneIndices() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetRenderBoneIndices(),
			ClothCollection->GetRenderVerticesStart(),
			ClothCollection->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TConstArrayView<TArray<float>> FCollectionClothRenderPatternConstFacade::GetRenderBoneWeights() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetRenderBoneWeights(),
			ClothCollection->GetRenderVerticesStart(),
			ClothCollection->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	int32 FCollectionClothRenderPatternConstFacade::GetNumRenderFaces() const
	{
		return ClothCollection->GetNumElements(
			ClothCollection->GetRenderFacesStart(),
			ClothCollection->GetRenderFacesEnd(),
			GetElementIndex());
	}

	int32 FCollectionClothRenderPatternConstFacade::GetRenderFacesOffset() const
	{
		return ClothCollection->GetElementsOffset(
			ClothCollection->GetRenderFacesStart(),
			GetBaseElementIndex(),
			GetElementIndex());
	}

	TConstArrayView<FIntVector3> FCollectionClothRenderPatternConstFacade::GetRenderIndices() const
	{
		return ClothCollection->GetElements(
			ClothCollection->GetRenderIndices(),
			ClothCollection->GetRenderFacesStart(),
			ClothCollection->GetRenderFacesEnd(),
			GetElementIndex());
	}

	FCollectionClothRenderPatternConstFacade::FCollectionClothRenderPatternConstFacade(const TSharedPtr<const FClothCollection>& ClothCollection, int32 InPatternIndex)
		: ClothCollection(ClothCollection)
		, PatternIndex(InPatternIndex)
	{
		check(ClothCollection.IsValid());
		check(ClothCollection->IsValid());
		check(PatternIndex >= 0 && PatternIndex < ClothCollection->GetNumElements(FClothCollection::RenderPatternsGroup));
	}

	void FCollectionClothRenderPatternFacade::Reset()
	{
		SetNumRenderVertices(0);
		SetNumRenderFaces(0);
		SetDefaults();
	}

	void FCollectionClothRenderPatternFacade::Initialize(const FCollectionClothRenderPatternConstFacade& Other)
	{
		Reset();

		//~ Render Vertices Group
		SetNumRenderVertices(Other.GetNumRenderVertices());
		FClothCollection::CopyArrayViewData(GetRenderPosition(), Other.GetRenderPosition());
		FClothCollection::CopyArrayViewData(GetRenderNormal(), Other.GetRenderNormal());
		FClothCollection::CopyArrayViewData(GetRenderTangentU(), Other.GetRenderTangentU());
		FClothCollection::CopyArrayViewData(GetRenderTangentV(), Other.GetRenderTangentV());
		FClothCollection::CopyArrayViewData(GetRenderUVs(), Other.GetRenderUVs());
		FClothCollection::CopyArrayViewData(GetRenderColor(), Other.GetRenderColor());
		FClothCollection::CopyArrayViewData(GetRenderBoneIndices(), Other.GetRenderBoneIndices());
		FClothCollection::CopyArrayViewData(GetRenderBoneWeights(), Other.GetRenderBoneWeights());

		//~ Render Faces Group
		const int32 RenderVertexOffset = GetRenderVerticesOffset() - Other.GetRenderVerticesOffset();
		SetNumRenderFaces(Other.GetNumRenderFaces());
		FClothCollection::CopyArrayViewDataAndApplyOffset(GetRenderIndices(), Other.GetRenderIndices(), FIntVector(RenderVertexOffset));

		SetRenderMaterialPathName(Other.GetRenderMaterialPathName());
	}

	void FCollectionClothRenderPatternFacade::SetRenderMaterialPathName(const FString& PathName)
	{
		(*GetClothCollection()->GetRenderMaterialPathName())[GetElementIndex()] = PathName;
	}

	void FCollectionClothRenderPatternFacade::SetNumRenderVertices(int32 NumRenderVertices)
	{
		GetClothCollection()->SetNumElements(
			NumRenderVertices,
			FClothCollection::RenderVerticesGroup,
			GetClothCollection()->GetRenderVerticesStart(),
			GetClothCollection()->GetRenderVerticesEnd(),
			GetElementIndex());
	}
	TArrayView<FVector3f> FCollectionClothRenderPatternFacade::GetRenderPosition()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetRenderPosition(),
			GetClothCollection()->GetRenderVerticesStart(),
			GetClothCollection()->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TArrayView<FVector3f> FCollectionClothRenderPatternFacade::GetRenderNormal()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetRenderNormal(),
			GetClothCollection()->GetRenderVerticesStart(),
			GetClothCollection()->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TArrayView<FVector3f> FCollectionClothRenderPatternFacade::GetRenderTangentU()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetRenderTangentU(),
			GetClothCollection()->GetRenderVerticesStart(),
			GetClothCollection()->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TArrayView<FVector3f> FCollectionClothRenderPatternFacade::GetRenderTangentV()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetRenderTangentV(),
			GetClothCollection()->GetRenderVerticesStart(),
			GetClothCollection()->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TArrayView<TArray<FVector2f>> FCollectionClothRenderPatternFacade::GetRenderUVs()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetRenderUVs(),
			GetClothCollection()->GetRenderVerticesStart(),
			GetClothCollection()->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TArrayView<FLinearColor> FCollectionClothRenderPatternFacade::GetRenderColor()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetRenderColor(),
			GetClothCollection()->GetRenderVerticesStart(),
			GetClothCollection()->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TArrayView<TArray<int32>> FCollectionClothRenderPatternFacade::GetRenderBoneIndices()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetRenderBoneIndices(),
			GetClothCollection()->GetRenderVerticesStart(),
			GetClothCollection()->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	TArrayView<TArray<float>> FCollectionClothRenderPatternFacade::GetRenderBoneWeights()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetRenderBoneWeights(),
			GetClothCollection()->GetRenderVerticesStart(),
			GetClothCollection()->GetRenderVerticesEnd(),
			GetElementIndex());
	}

	void FCollectionClothRenderPatternFacade::SetNumRenderFaces(int32 NumRenderFaces)
	{
		GetClothCollection()->SetNumElements(
			NumRenderFaces,
			FClothCollection::RenderFacesGroup,
			GetClothCollection()->GetRenderFacesStart(),
			GetClothCollection()->GetRenderFacesEnd(),
			GetElementIndex());
	}

	TArrayView<FIntVector3> FCollectionClothRenderPatternFacade::GetRenderIndices()
	{
		return GetClothCollection()->GetElements(
			GetClothCollection()->GetRenderIndices(),
			GetClothCollection()->GetRenderFacesStart(),
			GetClothCollection()->GetRenderFacesEnd(),
			GetElementIndex());
	}

	FCollectionClothRenderPatternFacade::FCollectionClothRenderPatternFacade(const TSharedPtr<FClothCollection>& ClothCollection, int32 InPatternIndex)
		: FCollectionClothRenderPatternConstFacade(ClothCollection, InPatternIndex)
	{
	}

	void FCollectionClothRenderPatternFacade::SetDefaults()
	{
		const int32 ElementIndex = GetElementIndex();

		(*GetClothCollection()->GetRenderVerticesStart())[ElementIndex] = INDEX_NONE;
		(*GetClothCollection()->GetRenderVerticesEnd())[ElementIndex] = INDEX_NONE;
		(*GetClothCollection()->GetRenderFacesStart())[ElementIndex] = INDEX_NONE;
		(*GetClothCollection()->GetRenderFacesEnd())[ElementIndex] = INDEX_NONE;
	}
}  // End namespace UE::Chaos::ClothAsset
