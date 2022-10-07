// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GeometryCollection.cpp: FGeometryCollection methods.
=============================================================================*/

#include "GeometryCollection/Facades/CollectionVertexBoneWeightsFacade.h"
#include "GeometryCollection/Facades/CollectionKinematicBindingFacade.h"
#include "GeometryCollection/GeometryCollection.h"

namespace GeometryCollection::Facades
{

	// Attributes
	const FName FVertexBoneWeightsFacade::WeightAttribute = "BoneWeights";
	const FName FVertexBoneWeightsFacade::IndexAttribute = "BoneWeightsIndex";

	FVertexBoneWeightsFacade::FVertexBoneWeightsFacade(FManagedArrayCollection* InCollection)
		: Self(InCollection)
	{
		DefineSchema(Self);
	}

	//
	//  Initialization
	//

		//
	//  Initialization
	//

	void FVertexBoneWeightsFacade::DefineSchema(FManagedArrayCollection* Collection)
	{
		FManagedArrayCollection::FConstructionParameters TransformDependency(FTransformCollection::TransformGroup);

		if (!Collection->HasGroup(FGeometryCollection::VerticesGroup))
		{
			return;
		}

		Collection->AddAttribute<TArray<int32>>(FVertexBoneWeightsFacade::IndexAttribute, FGeometryCollection::VerticesGroup, TransformDependency);
		Collection->AddAttribute<TArray<float>>(FVertexBoneWeightsFacade::WeightAttribute, FGeometryCollection::VerticesGroup, TransformDependency);

		ensure(Collection->FindAttributeTyped<TArray<int32>>(FVertexBoneWeightsFacade::IndexAttribute, FGeometryCollection::VerticesGroup) != nullptr);
		ensure(Collection->FindAttributeTyped<TArray<float>>(FVertexBoneWeightsFacade::WeightAttribute, FGeometryCollection::VerticesGroup) != nullptr);
	}

	bool FVertexBoneWeightsFacade::HasFacade(const FManagedArrayCollection* Collection)
	{
		return Collection->HasGroup(FGeometryCollection::VerticesGroup) &&
			Collection->FindAttributeTyped<TArray<int32>>(FVertexBoneWeightsFacade::IndexAttribute, FGeometryCollection::VerticesGroup) &&
			Collection->FindAttributeTyped<TArray<float>>(FVertexBoneWeightsFacade::WeightAttribute, FGeometryCollection::VerticesGroup);
	}


	//
	//  Add Weights from Selection 
	//  ... ... (Impressionist physbam)
	//

	void FVertexBoneWeightsFacade::AddBoneWeightsFromKinematicBindings(FManagedArrayCollection* InCollection) {
		if (InCollection && InCollection->HasGroup(FGeometryCollection::VerticesGroup)) {
			DefineSchema(InCollection); TArray<float> Weights; TArray<int32> Indices;
			int32 NumBones = InCollection->NumElements(FTransformCollection::TransformGroup), NumVertices = InCollection->NumElements(FGeometryCollection::VerticesGroup);
			TManagedArray< TArray<int32> >& IndicesArray = InCollection->ModifyAttribute<TArray<int32>>(FVertexBoneWeightsFacade::IndexAttribute, FGeometryCollection::VerticesGroup);
			TManagedArray< TArray<float> >& WeightsArray = InCollection->ModifyAttribute<TArray<float>>(FVertexBoneWeightsFacade::WeightAttribute, FGeometryCollection::VerticesGroup);
			for (int32 Kdx = Chaos::Facades::FKinematicBindingFacade::NumKinematicBindings(InCollection) - 1; 0 <= Kdx; Kdx--) {
				int32 Bone; TArray<int32> OutBoneVerts; TArray<float> OutBoneWeights;
				Chaos::Facades::FKinematicBindingFacade::GetBoneBindings(InCollection, Chaos::Facades::FKinematicBindingFacade::GetKinematicBindingKey(InCollection, Kdx), Bone, OutBoneVerts, OutBoneWeights);
				if (0 <= Bone && Bone < NumBones) for (int32 Vdx = 0; Vdx < OutBoneVerts.Num(); Vdx++) { int32 Vert = OutBoneVerts[Vdx]; float Weight = OutBoneWeights[Vdx];
					if (0 <= Vert && Vert < NumVertices && !IndicesArray[Vert].Contains(Bone)) { IndicesArray[Vert].Add(Bone); WeightsArray[Vert].Add(Weight); }}}}}

	//
	//  GetAttributes
	//

	const TManagedArray< TArray<int32> >* FVertexBoneWeightsFacade::GetBoneIndices(const FManagedArrayCollection* Collection)
	{
		return Collection->FindAttribute<TArray<int32>>(FVertexBoneWeightsFacade::IndexAttribute, FGeometryCollection::VerticesGroup);
	}

	const TManagedArray< TArray<float> >* FVertexBoneWeightsFacade::GetBoneWeights(const FManagedArrayCollection* Collection)
	{
		return Collection->FindAttribute<TArray<float>>(FVertexBoneWeightsFacade::WeightAttribute, FGeometryCollection::VerticesGroup);
	}


};


