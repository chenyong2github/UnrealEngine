// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GeometryCollection.cpp: FGeometryCollection methods.
=============================================================================*/

#include "GeometryCollection/Facades/CollectionBoundsFacade.h"
#include "GeometryCollection/GeometryCollection.h"

namespace GeometryCollection::Facades
{

	// Attributes
	const FName FBoundsFacade::AAAAttribute = "AAA";

	FBoundsFacade::FBoundsFacade(FManagedArrayCollection& InCollection)
		: Self(InCollection)
		, BoundingBox(InCollection,"BoundingBox", FGeometryCollection::GeometryGroup)
		, Vertex(InCollection,"Vertex", FGeometryCollection::VerticesGroup)
		, BoneMap(InCollection,"BoneMap", FGeometryCollection::VerticesGroup)
		, TransformToGeometryIndex(InCollection,"TransformToGeometryIndex", FTransformCollection::TransformGroup)
	{
		DefineSchema(Self);
	}

	//
	//  Initialization
	//

		//
	//  Initialization
	//

	void FBoundsFacade::DefineSchema(FManagedArrayCollection& InCollection)
	{
		FManagedArrayCollection::FConstructionParameters VertexDependency(FGeometryCollection::VerticesGroup);

		// surface rendering attributes
		if(!InCollection.HasGroup(FGeometryCollection::VerticesGroup))
		{
			InCollection.AddGroup(FGeometryCollection::VerticesGroup);
		}
		if( !InCollection.HasGroup(FGeometryCollection::FacesGroup) )
		{
			InCollection.AddGroup(FGeometryCollection::FacesGroup);
		}

		InCollection.AddAttribute<FVector3f>("Vertex", FGeometryCollection::VerticesGroup);
		InCollection.AddAttribute<FIntVector>("Indices", FGeometryCollection::FacesGroup, VertexDependency);

		ensure(InCollection.FindAttributeTyped<FVector3f>("Vertex", FGeometryCollection::VerticesGroup) != nullptr);
		ensure(InCollection.FindAttributeTyped<FIntVector>("Indices", FGeometryCollection::FacesGroup) != nullptr);
	}

	bool FBoundsFacade::IsValid(const FManagedArrayCollection& InCollection)
	{
		return InCollection.HasAttribute("BoundingBox", FGeometryCollection::GeometryGroup)
			&& InCollection.HasAttribute("Vertex", FGeometryCollection::VerticesGroup)
			&& InCollection.HasAttribute("BoneMap", FGeometryCollection::VerticesGroup)
			&& InCollection.HasAttribute("TransformToGeometryIndex", FTransformCollection::TransformGroup);
	}



	void FBoundsFacade::UpdateBoundingBox(FManagedArrayCollection& InCollection, bool bSkipCheck)
	{
		if (!bSkipCheck || !IsValid(InCollection))
		{
			return;
		}

		TManagedArray<FBox>& BoundingBox = InCollection.ModifyAttribute<FBox>("BoundingBox", FGeometryCollection::GeometryGroup);
		const TManagedArray<FVector3f>& Vertex = InCollection.GetAttribute<FVector3f>("Vertex", FGeometryCollection::VerticesGroup);
		const TManagedArray<int32>& BoneMap = InCollection.GetAttribute<int32>("BoneMap", FGeometryCollection::VerticesGroup);
		const TManagedArray<int32>& TransformToGeometryIndex = InCollection.GetAttribute<int32>("TransformToGeometryIndex", FTransformCollection::TransformGroup);

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

	//
	//  GetAttributes
	//

	const TManagedArray< FBox >* FBoundsFacade::GetBoundingBoxes(const FManagedArrayCollection& InCollection)
	{
		return InCollection.FindAttributeTyped <FBox>("BoundingBox", FGeometryCollection::GeometryGroup);
	}


}; // GeometryCollection::Facades


