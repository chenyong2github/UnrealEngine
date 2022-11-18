// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GeometryCollection.cpp: FGeometryCollection methods.
=============================================================================*/

#include "GeometryCollection/Facades/CollectionRenderingFacade.h"
#include "GeometryCollection/GeometryCollection.h"

namespace GeometryCollection::Facades
{

	FRenderingFacade::FRenderingFacade(FManagedArrayCollection& InCollection)
		: ConstCollection(InCollection)
		, Collection(&InCollection)
		, VertexAttribute(InCollection, "Vertex", FGeometryCollection::VerticesGroup)
		, IndicesAttribute(InCollection, "Indices", FGeometryCollection::FacesGroup, FGeometryCollection::VerticesGroup)
		, MaterialIDAttribute(InCollection, "MaterialID", FGeometryCollection::FacesGroup)
		, TriangleSectionAttribute(InCollection, "Sections", FGeometryCollection::MaterialGroup)
	{}

	FRenderingFacade::FRenderingFacade(const FManagedArrayCollection& InCollection)
		: ConstCollection(InCollection)
		, Collection(nullptr)
		, VertexAttribute(InCollection, "Vertex", FGeometryCollection::VerticesGroup)
		, IndicesAttribute(InCollection, "Indices", FGeometryCollection::FacesGroup, FGeometryCollection::VerticesGroup)
		, MaterialIDAttribute(InCollection, "MaterialID", FGeometryCollection::FacesGroup)
		, TriangleSectionAttribute(InCollection, "Sections", FGeometryCollection::MaterialGroup)
	{}

	//
	//  Initialization
	//

	void FRenderingFacade::DefineSchema()
	{
		check(!IsConst());
		VertexAttribute.Add();
		IndicesAttribute.Add();
		MaterialIDAttribute.Add();
		TriangleSectionAttribute.Add();
	}

	bool FRenderingFacade::CanRenderSurface( ) const
	{
		return  IsValid() && GetIndices().Num() && GetVertices().Num();
	}

	bool FRenderingFacade::IsValid( ) const
	{
		return VertexAttribute.IsValid() && IndicesAttribute.IsValid() &&
			MaterialIDAttribute.IsValid() && TriangleSectionAttribute.IsValid();
	}

	void FRenderingFacade::AddTriangle(const Chaos::FTriangle& InTriangle)
	{
		check(!IsConst());
		if (IsValid())
		{
			auto CollectionVert = [](const Chaos::FVec3& V) { return FVector3f(float(V.X), float(V.Y), float(V.Z)); };

			TManagedArray<FVector3f>& Vertices = VertexAttribute.Modify();
			TManagedArray<FIntVector>& Indices = IndicesAttribute.Modify();
			
			int32 IndicesStart = IndicesAttribute.AddElements(1);
			int32 VertexStart = VertexAttribute.AddElements(3);

			Indices[IndicesStart] = FIntVector(VertexStart, VertexStart + 1, VertexStart + 2);
			Vertices[VertexStart] = CollectionVert(InTriangle[0]);
			Vertices[VertexStart + 1] = CollectionVert(InTriangle[1]);
			Vertices[VertexStart + 2] = CollectionVert(InTriangle[2]);
		}
	}


	void FRenderingFacade::AddSurface(TArray<FVector3f>&& InVertices, TArray<FIntVector>&& InIndices)
	{
		check(!IsConst());
		if (IsValid())
		{
			auto CollectionVert = [](const Chaos::FVec3& V) { return FVector3f(float(V.X), float(V.Y), float(V.Z)); };

			TManagedArray<FVector3f>& Vertices = VertexAttribute.Modify();
			TManagedArray<FIntVector>& Indices = IndicesAttribute.Modify();

			int32 IndicesStart = IndicesAttribute.AddElements(InIndices.Num());
			int32 VertexStart = VertexAttribute.AddElements(InVertices.Num());
			
			const FIntVector * IndiciesDest = Indices.GetData() + IndicesStart;
			FMemory::Memmove((void*)IndiciesDest, InIndices.GetData(), sizeof(FIntVector) * InIndices.Num());

			const FVector3f * VerticesDest = Vertices.GetData() + VertexStart;
			FMemory::Memmove((void*)VerticesDest, InVertices.GetData(), sizeof(FVector3f) * InVertices.Num());
		}
	}


	TArray<FRenderingFacade::FTriangleSection> 
	FRenderingFacade::BuildMeshSections(const TArray<FIntVector>& InputIndices, TArray<int32> BaseMeshOriginalIndicesIndex, TArray<FIntVector>& RetIndices) const
	{
		check(!IsConst());
		return FGeometryCollectionSection::BuildMeshSections(ConstCollection, InputIndices, BaseMeshOriginalIndicesIndex, RetIndices);
	}

}; // GeometryCollection::Facades


