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
	* FBoundsFacade
	* 
	* Defines common API for calculating the bounding box on a collection
	* 
	* Usage:
	*    FBoundsFacade::UpdateBoundingBox(this);
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
	class CHAOS_API FBoundsFacade
	{
		FManagedArrayCollection& Self;

	public:

		// Attributes
		static const FName AAAAttribute;

		/**
		* FBoundsFacade Constuctor
		* @param VertixDependencyGroup : GroupName the index attribute is dependent on.
		*/
		FBoundsFacade(FManagedArrayCollection& InSelf);

		/**
		*  Create the facade.
		*/
		static void DefineSchema(FManagedArrayCollection& InCollection);
		void DefineSchema() { return DefineSchema(Self); }


		/**
		*  Is the Facade defined on the collection?
		*/
		static bool IsValid(const FManagedArrayCollection& InCollection);
		bool IsValid() { return IsValid(Self); }


		/**
		*  Does it support rendering surfaces. 
		*/
		static void UpdateBoundingBox(FManagedArrayCollection& InCollection, bool bSkipCheck = false);
		void UpdateBoundingBox(bool bSkipCheck = false) { UpdateBoundingBox(Self); }


		/**
		* Return the vertex positions from the collection. Null if not initialized.
		* @param FManagedArrayCollection : Collection
		*/
		static const TManagedArray< FBox >* GetBoundingBoxes(const FManagedArrayCollection& InCollection);
		const TManagedArray< FBox >* GetBoundingBoxes() const { return GetBoundingBoxes(Self); }

private:
		TManagedArrayAccessor<FBox> BoundingBox;
		TManagedArrayAccessor<FVector3f> Vertex;
		TManagedArrayAccessor<int32> BoneMap;
		TManagedArrayAccessor<int32> TransformToGeometryIndex;
	};

}