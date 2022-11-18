// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GeometryCollection.cpp: FGeometryCollection methods.
=============================================================================*/

#include "GeometryCollection/Facades/CollectionBoundsFacade.h"
#include "GeometryCollection/GeometryCollection.h"

namespace GeometryCollection::Facades
{

	FBoundsFacade::FBoundsFacade(FManagedArrayCollection& InCollection)
		: BoundingBoxAttribute(InCollection,"BoundingBox", FGeometryCollection::GeometryGroup)
		, VertexAttribute(InCollection,"Vertex", FGeometryCollection::VerticesGroup, FGeometryCollection::VerticesGroup)
		, BoneMapAttribute(InCollection,"BoneMap", FGeometryCollection::VerticesGroup)
		, TransformToGeometryIndexAttribute(InCollection,"TransformToGeometryIndex", FTransformCollection::TransformGroup)
	{}

	FBoundsFacade::FBoundsFacade(const FManagedArrayCollection& InCollection)
		: BoundingBoxAttribute(InCollection, "BoundingBox", FGeometryCollection::GeometryGroup)
		, VertexAttribute(InCollection, "Vertex", FGeometryCollection::VerticesGroup, FGeometryCollection::VerticesGroup)
		, BoneMapAttribute(InCollection, "BoneMap", FGeometryCollection::VerticesGroup)
		, TransformToGeometryIndexAttribute(InCollection, "TransformToGeometryIndex", FTransformCollection::TransformGroup)
	{}

	//
	//  Initialization
	//

	void FBoundsFacade::DefineSchema()
	{
		check(!IsConst());
		BoundingBoxAttribute.Add();
		VertexAttribute.Add();
		BoneMapAttribute.Add();
		TransformToGeometryIndexAttribute.Add();
	}

	bool FBoundsFacade::IsValid() const
	{
		return BoundingBoxAttribute.IsValid() && VertexAttribute.IsValid() 
			&& BoneMapAttribute.IsValid() && TransformToGeometryIndexAttribute.IsValid();
	}


	void FBoundsFacade::UpdateBoundingBox(bool bSkipCheck)
	{
		check(!IsConst());

		if (!bSkipCheck || !IsValid())
		{
			return;
		}

		TManagedArray<FBox>& BoundingBox = BoundingBoxAttribute.Modify();
		const TManagedArray<FVector3f>& Vertex = VertexAttribute.Get();
		const TManagedArray<int32>& BoneMap = BoneMapAttribute.Get();
		const TManagedArray<int32>& TransformToGeometryIndex = TransformToGeometryIndexAttribute.Get();

		if (BoundingBox.Num())
		{
			// Initialize BoundingBox
			for (int32 Idx = 0; Idx < BoundingBox.Num(); ++Idx)
			{
				BoundingBox[Idx].Init();
			}

			// Compute BoundingBox
			for (int32 Idx = 0; Idx < Vertex.Num(); ++Idx)
			{
				int32 TransformIndexValue = BoneMap[Idx];
				BoundingBox[TransformToGeometryIndex[TransformIndexValue]] += FVector(Vertex[Idx]);
			}
		}
	}
}; // GeometryCollection::Facades


