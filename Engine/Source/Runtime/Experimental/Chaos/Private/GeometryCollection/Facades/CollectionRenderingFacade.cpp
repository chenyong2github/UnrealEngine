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
		, VertexToGeometryIndexAttribute(InCollection, "GeometryIndex", FGeometryCollection::VerticesGroup, FGeometryCollection::GeometryGroup)
		, IndicesAttribute(InCollection, "Indices", FGeometryCollection::FacesGroup, FGeometryCollection::VerticesGroup)
		, MaterialIDAttribute(InCollection, "MaterialID", FGeometryCollection::FacesGroup)
		, TriangleSectionAttribute(InCollection, "Sections", FGeometryCollection::MaterialGroup)
		, GeometryNameAttribute(InCollection, "Name", FGeometryCollection::GeometryGroup)
		, HitProxyIndexAttribute(InCollection, "HitIndex", FGeometryCollection::GeometryGroup)
		, VertixStartAttribute(InCollection, "VertexStart", FGeometryCollection::GeometryGroup)
		, VertixCountAttribute(InCollection, "VertexCount", FGeometryCollection::GeometryGroup)
		, IndicesStartAttribute(InCollection, "IndicesStart", FGeometryCollection::GeometryGroup)
		, IndicesCountAttribute(InCollection, "IndicesCount", FGeometryCollection::GeometryGroup)
		, SelectionState(InCollection, "SelectionState", FGeometryCollection::GeometryGroup)

	{}

	FRenderingFacade::FRenderingFacade(const FManagedArrayCollection& InCollection)
		: ConstCollection(InCollection)
		, Collection(nullptr)
		, VertexAttribute(InCollection, "Vertex", FGeometryCollection::VerticesGroup)
		, VertexToGeometryIndexAttribute(InCollection, "GeometryIndex", FGeometryCollection::VerticesGroup, FGeometryCollection::GeometryGroup)
		, IndicesAttribute(InCollection, "Indices", FGeometryCollection::FacesGroup, FGeometryCollection::VerticesGroup)
		, MaterialIDAttribute(InCollection, "MaterialID", FGeometryCollection::FacesGroup)
		, TriangleSectionAttribute(InCollection, "Sections", FGeometryCollection::MaterialGroup)
		, GeometryNameAttribute(InCollection, "Name", FGeometryCollection::GeometryGroup)
		, HitProxyIndexAttribute(InCollection, "HitIndex", FGeometryCollection::GeometryGroup)
		, VertixStartAttribute(InCollection, "VertexStart", FGeometryCollection::GeometryGroup, FGeometryCollection::VerticesGroup)
		, VertixCountAttribute(InCollection, "VertexCount", FGeometryCollection::GeometryGroup)
		, IndicesStartAttribute(InCollection, "IndicesStart", FGeometryCollection::GeometryGroup, FGeometryCollection::FacesGroup)
		, IndicesCountAttribute(InCollection, "IndicesCount", FGeometryCollection::GeometryGroup)
		, SelectionState(InCollection, "SelectionState", FGeometryCollection::GeometryGroup)
	{}

	//
	//  Initialization
	//

	void FRenderingFacade::DefineSchema()
	{
		check(!IsConst());
		VertexAttribute.Add();
		VertexToGeometryIndexAttribute.Add();
		IndicesAttribute.Add();
		MaterialIDAttribute.Add();
		TriangleSectionAttribute.Add();
		GeometryNameAttribute.Add();
		HitProxyIndexAttribute.Add();
		VertixStartAttribute.Add();
		VertixCountAttribute.Add();
		IndicesStartAttribute.Add();
		IndicesCountAttribute.Add();
		SelectionState.Add();
	}

	bool FRenderingFacade::CanRenderSurface( ) const
	{
		return  IsValid() && GetIndices().Num() && GetVertices().Num();
	}

	bool FRenderingFacade::IsValid( ) const
	{
		return VertexAttribute.IsValid() && VertexToGeometryIndexAttribute.IsValid() &&
			IndicesAttribute.IsValid() &&
			MaterialIDAttribute.IsValid() && TriangleSectionAttribute.IsValid() &&
			GeometryNameAttribute.IsValid() && HitProxyIndexAttribute.IsValid() &&
			VertixStartAttribute.IsValid() && VertixCountAttribute.IsValid() &&
			IndicesStartAttribute.IsValid() && IndicesCountAttribute.IsValid() &&
			SelectionState.IsValid();
	}

	int32 FRenderingFacade::NumTriangles() const
	{
		if (IsValid())
		{
			return GetIndices().Num();
		}
			 
		return 0;
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
			
			FIntVector * IndiciesDest = Indices.GetData() + IndicesStart;
			FMemory::Memmove((void*)IndiciesDest, InIndices.GetData(), sizeof(FIntVector) * InIndices.Num());

			for (int i = IndicesStart; i < IndicesStart + InIndices.Num(); i++)
			{
				Indices[i][0] += VertexStart;
				Indices[i][1] += VertexStart;
				Indices[i][2] += VertexStart;
			}

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


	int32 FRenderingFacade::StartGeometryGroup(FString InName)
	{
		check(!IsConst());

		int32 GeomIndex = INDEX_NONE;
		if (IsValid())
		{
			GeomIndex = GeometryNameAttribute.AddElements(1);
			GeometryNameAttribute.Modify()[GeomIndex] = InName;

			VertixStartAttribute.Modify()[GeomIndex] = VertexAttribute.Num();
			VertixCountAttribute.Modify()[GeomIndex] = 0;
			IndicesStartAttribute.Modify()[GeomIndex] = IndicesAttribute.Num();
			IndicesCountAttribute.Modify()[GeomIndex] = 0;
			SelectionState.Modify()[GeomIndex] = 0;
		}
		return GeomIndex;
	}

	void FRenderingFacade::EndGeometryGroup(int32 InGeomIndex)
	{
		check(!IsConst());
		if (IsValid())
		{
			check( GeometryNameAttribute.Num()-1 == InGeomIndex );

			if (VertixStartAttribute.Get()[InGeomIndex] < VertexAttribute.Num())
			{
				VertixCountAttribute.Modify()[InGeomIndex] = VertexAttribute.Num() - VertixStartAttribute.Get()[InGeomIndex];

				TManagedArray<int32>& GeomIndexAttr = VertexToGeometryIndexAttribute.Modify();
				for (int i = VertixStartAttribute.Get()[InGeomIndex]; i < VertexAttribute.Num(); i++)
				{
					GeomIndexAttr[i] = InGeomIndex;
				}
			}
			else
			{
				VertixStartAttribute.Modify()[InGeomIndex] = VertexAttribute.Num();
			}

			if (IndicesStartAttribute.Get()[InGeomIndex] < IndicesAttribute.Num())
			{
				IndicesCountAttribute.Modify()[InGeomIndex] = IndicesAttribute.Num() - IndicesStartAttribute.Get()[InGeomIndex];
			}
			else
			{
				IndicesStartAttribute.Modify()[InGeomIndex] = IndicesAttribute.Num();
			}
		}
	}

	FRenderingFacade::FStringIntMap FRenderingFacade::GetGeometryNameToIndexMap() const
	{
		FStringIntMap Map;
		for (int32 i = 0; i < GeometryNameAttribute.Num(); i++)
		{
			Map.Add(GetGeometryNameAttribute()[i], i);
		}
		return Map;
	}


}; // GeometryCollection::Facades


