// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Containers/UnrealString.h"
#include "Containers/Array.h"
#include "Math/MathFwd.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/Facades/CollectionSelectionFacade.h"
#include "Chaos/Triangle.h"

namespace GeometryCollection::Facades
{

	/**
	* FRenderingFacade
	*
	* Defines common API for storing a vertex weights bound to a bone. This mapping is from the
	* the vertex to the bone index. The FSelectionFacad will store the mapping from the BoneIndex
	* to the vertex.
	* Usage:
	*    FRenderingFacade::AddBoneWeights(this, FSelectionFacade(this) );
	*
	* Then arrays can be accessed later by:
	*	const TManagedArray< FIntVector >* FaceIndices = FRenderingFacade::GetIndices(this);
	*	const TManagedArray< FVector3f >* Vertices = FRenderingFacade::GetVertices(this);
	*
	* The following attributes are created on the collection:
	*
	*	- FindAttribute<FIntVector>("Indices", FGeometryCollection::FacesGroup);
	*	- FindAttribute<FVector3f>("Vertex", FGeometryCollection::VerticesGroup);
	*
	*/
	class CHAOS_API FRenderingFacade
	{
		FManagedArrayCollection* Self;

	public:

		// Attributes
		static const FName AAAAttribute;

		/**
		* FRenderingFacade Constuctor
		* @param VertixDependencyGroup : GroupName the index attribute is dependent on.
		*/
		FRenderingFacade(FManagedArrayCollection* InSelf);

		/**
		*  Create the facade.
		*/
		static void DefineSchema(FManagedArrayCollection* InCollection);
		void DefineSchema() { return DefineSchema(Self); }


		/**
		*  Is the Facade defined on the collection?
		*/
		static bool IsValid(const FManagedArrayCollection* InCollection);
		bool IsValid() { return IsValid(Self); }


		/**
		*  Does it support rendering surfaces.
		*/
		static bool CanRenderSurface(const FManagedArrayCollection* InCollection);
		bool CanRenderSurface() { return CanRenderSurface(Self); }


		/**
		* Using a FSelectionFacade::FSelectionKey, create indexes from the vertices to the driving bones.
		* @param FManagedArrayCollection : Collection
		* @param FSelectionFacade::FSelectionKey : Key for weights in the FSelectionFacade
		*/
		static void AddTriangle(FManagedArrayCollection* InCollection, const Chaos::FTriangle& InTriangle);
		void AddTriangle(const Chaos::FTriangle& InTriangle) { AddTriangle(Self, InTriangle); }

		/**
		* Return the render indices from the collection. Null if not initialized.
		* @param FManagedArrayCollection : Collection
		*/
		static const TManagedArray< FIntVector >* GetIndices(const FManagedArrayCollection* InCollection);
		const TManagedArray< FIntVector >* GetIndices() const { return GetIndices(Self); }


		/**
		* Return the vertex positions from the collection. Null if not initialized.
		* @param FManagedArrayCollection : Collection
		*/
		static const TManagedArray< FVector3f >* GetVertices(const FManagedArrayCollection* InCollection);
		const TManagedArray< FVector3f >* GetVertices() const { return GetVertices(Self); }


	};

}