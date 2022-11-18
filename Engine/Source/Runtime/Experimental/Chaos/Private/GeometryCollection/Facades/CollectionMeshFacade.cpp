// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GeometryCollection.cpp: FGeometryCollection methods.
=============================================================================*/

#include "GeometryCollection/Facades/CollectionMeshFacade.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/TransformCollection.h"

namespace GeometryCollection::Facades
{
	FMeshFacade::FMeshFacade(FManagedArrayCollection& InCollection)
		: Vertex(InCollection, "Vertex", FGeometryCollection::VerticesGroup)
		, TangentU(InCollection, "TangentU", FGeometryCollection::VerticesGroup)
		, TangentV(InCollection, "TangentV", FGeometryCollection::VerticesGroup)
		, Normal(InCollection, "Normal", FGeometryCollection::VerticesGroup)
		, UVs(InCollection, "UVs", FGeometryCollection::VerticesGroup)
		, Color(InCollection, "Color", FGeometryCollection::VerticesGroup)
		, BoneMap(InCollection, "BoneMap", FGeometryCollection::VerticesGroup)
		, VertexStart(InCollection, "VertexStart", FGeometryCollection::GeometryGroup)
		, VertexCount(InCollection, "VertexCount", FGeometryCollection::GeometryGroup)
		, Indices(InCollection, "Indices", FGeometryCollection::FacesGroup)
		, Visible(InCollection, "Visible", FGeometryCollection::FacesGroup)
		, MaterialIndex(InCollection, "MaterialIndex", FGeometryCollection::FacesGroup)
		, MaterialID(InCollection, "MaterialID", FGeometryCollection::FacesGroup)
		, FaceStart(InCollection, "FaceStart", FGeometryCollection::GeometryGroup)
		, FaceCount(InCollection, "FaceCount", FGeometryCollection::GeometryGroup)
	{
	}

	bool FMeshFacade::IsValid() const
	{
		return Vertex.IsValid()
			&& TangentU.IsValid()
			&& TangentV.IsValid()
			&& Normal.IsValid()
			&& UVs.IsValid()
			&& Color.IsValid()
			&& BoneMap.IsValid()
			&& VertexStart.IsValid()
			&& VertexCount.IsValid()
			&& Indices.IsValid()
			&& Visible.IsValid()
			&& MaterialIndex.IsValid()
			&& MaterialID.IsValid()
			&& FaceStart.IsValid()
			&& FaceCount.IsValid()
			;
	}

	void FMeshFacade::DefineSchema()
	{
		Vertex.Add();
		TangentU.Add();
		TangentV.Add();
		Normal.Add();
		UVs.Add();
		Color.Add();
		BoneMap.Add();
		VertexStart.Add();
		VertexCount.Add();
		Indices.Add();
		Visible.Add();
		MaterialIndex.Add();
		MaterialID.Add();
		FaceStart.Add();
		FaceCount.Add();
	}
};


