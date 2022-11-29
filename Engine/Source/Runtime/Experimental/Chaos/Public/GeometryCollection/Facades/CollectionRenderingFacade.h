// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Containers/UnrealString.h"
#include "Containers/Array.h"
#include "Math/MathFwd.h"
#include "GeometryCollection/ManagedArrayAccessor.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/Facades/CollectionSelectionFacade.h"
#include "Chaos/Triangle.h"

namespace GeometryCollection::Facades
{

	/**
	* FRenderingFacade
	*
	* Defines common API for storing rendering data.
	*
	*/
	class CHAOS_API FRenderingFacade
	{
	public:
		typedef FGeometryCollectionSection FTriangleSection;

		/**
		* FRenderingFacade Constuctor
		* @param VertixDependencyGroup : GroupName the index attribute is dependent on.
		*/
		FRenderingFacade(FManagedArrayCollection& InSelf);
		FRenderingFacade(const FManagedArrayCollection& InSelf);

		/**Create the facade.*/
		void DefineSchema();

		/** Is the facade defined constant. */
		bool IsConst() const { return Collection==nullptr; }

		/**Is the Facade defined on the collection?*/
		bool IsValid() const;

		/**Does it support rendering surfaces.*/
		bool CanRenderSurface() const;

		//
		// Facade API
		//

		/**Number of triangles to render.*/
		int32 NumTriangles() const;

		/**Add a triangle to the rendering view.*/
		void AddTriangle(const Chaos::FTriangle& InTriangle);

		/** Add a surface to the rendering view.*/
		void AddSurface(TArray<FVector3f>&& InVertices, TArray<FIntVector>&& InIndices);
		
		/** GetIndices */
		const TManagedArray< FIntVector >& GetIndices() const { return IndicesAttribute.Get(); }

		/** GetVertices */
		const TManagedArray< FVector3f >& GetVertices() const { return VertexAttribute.Get(); }

		/** GetVertexToGeometryIndexAttribute */
		const TManagedArray< int32 >& GetVertexToGeometryIndexAttribute() const { return VertexToGeometryIndexAttribute.Get(); }

		/** GetMaterialID */
		const TManagedArray< int32 >& GetMaterialID() const { return MaterialIDAttribute.Get(); }

		/** GetTriangleSections */
		const TManagedArray< FTriangleSection >& GetTriangleSections() const { return TriangleSectionAttribute.Get(); }

		/** BuildMeshSections */
		TArray<FTriangleSection> BuildMeshSections(const TArray<FIntVector>& Indices, TArray<int32> BaseMeshOriginalIndicesIndex, TArray<FIntVector>& RetIndices) const;
		
		/** Geometry Group Start : */
		int32 StartGeometryGroup(FString InName);

		/** Geometry Group End : */
		void EndGeometryGroup(int32 InGeometryGroupIndex);

		int32 NumGeometry() const { return GeometryNameAttribute.Num(); }

		/** GeometryNameAttribute */
		const TManagedArray< FString >& GetGeometryNameAttribute() const { return GeometryNameAttribute.Get(); }

		/** HitProxyIDAttribute */
		const TManagedArray< int32>& GetHitProxyIndexAttribute() const { return HitProxyIndexAttribute.Get(); }
		TManagedArray< int32 >& ModifyHitProxyIndexAttribute() {check(!IsConst());return HitProxyIndexAttribute.Modify(); }

		/** VertixStartAttribute */
		const TManagedArray< int32 >& GetVertixStartAttribute() const { return VertixStartAttribute.Get(); }

		/** VertixCountAttribute */
		const TManagedArray< int32 >& GetVertixCountAttribute() const { return VertixCountAttribute.Get(); }

		/** IndicesStartAttribute */
		const TManagedArray< int32 >& GetIndicesStartAttribute() const { return IndicesStartAttribute.Get(); }

		/** IndicesCountAttribute */
		const TManagedArray< int32 >& GetIndicesCountAttribute() const { return IndicesCountAttribute.Get(); }

	private : 
		const FManagedArrayCollection& ConstCollection;
		FManagedArrayCollection* Collection = nullptr;

		TManagedArrayAccessor<FVector3f> VertexAttribute;
		TManagedArrayAccessor<int32> VertexToGeometryIndexAttribute;
		TManagedArrayAccessor<FIntVector> IndicesAttribute;
		TManagedArrayAccessor<int32> MaterialIDAttribute;
		TManagedArrayAccessor<FTriangleSection> TriangleSectionAttribute;

		TManagedArrayAccessor<FString> GeometryNameAttribute;
		TManagedArrayAccessor<int32> HitProxyIndexAttribute;
		TManagedArrayAccessor<int32> VertixStartAttribute;
		TManagedArrayAccessor<int32> VertixCountAttribute;
		TManagedArrayAccessor<int32> IndicesStartAttribute;
		TManagedArrayAccessor<int32> IndicesCountAttribute;


	};

}