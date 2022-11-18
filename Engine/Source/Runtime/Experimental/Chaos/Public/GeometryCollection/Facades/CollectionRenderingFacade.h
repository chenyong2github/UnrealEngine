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

		/**Add a triangle to the rendering view.*/
		void AddTriangle(const Chaos::FTriangle& InTriangle);

		/** Add a surface to the rendering view.*/
		void AddSurface(TArray<FVector3f>&& InVertices, TArray<FIntVector>&& InIndices);
		
		/** GetIndices */
		const TManagedArray< FIntVector >& GetIndices() const { return IndicesAttribute.Get(); }

		/** GetVertices */
		const TManagedArray< FVector3f >& GetVertices() const { return VertexAttribute.Get(); }

		/** GetMaterialID */
		const TManagedArray< int32 >& GetMaterialID() const { return MaterialIDAttribute.Get(); }

		/** GetTriangleSections */
		const TManagedArray< FTriangleSection >& GetTriangleSections() const { return TriangleSectionAttribute.Get(); }

		/** BuildMeshSections */
		TArray<FTriangleSection> BuildMeshSections(const TArray<FIntVector>& Indices, TArray<int32> BaseMeshOriginalIndicesIndex, TArray<FIntVector>& RetIndices) const;
		
	private : 
		const FManagedArrayCollection& ConstCollection;
		FManagedArrayCollection* Collection = nullptr;

		TManagedArrayAccessor<FVector3f> VertexAttribute;
		TManagedArrayAccessor<FIntVector> IndicesAttribute;
		TManagedArrayAccessor<int32> MaterialIDAttribute;
		TManagedArrayAccessor<FTriangleSection> TriangleSectionAttribute;

	};

}