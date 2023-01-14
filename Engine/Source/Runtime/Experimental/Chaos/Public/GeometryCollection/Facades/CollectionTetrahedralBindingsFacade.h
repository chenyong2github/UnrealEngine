// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Containers/UnrealString.h"
#include "Containers/Array.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/ManagedArrayAccessor.h"

namespace GeometryCollection::Facades
{

	/**
	* FTetrahedralBindings
	* 
	* Interface for storing and retrieving bindings of surfaces (typically SkeletalMesh or StaticMesh) to
	* tetrahedral meshes.  Bindings data for each surface is grouped by a mesh id and a level of detail.
	*/
	class CHAOS_API FTetrahedralBindings
 	{
	public:

		// groups
		static const FName MeshBindingsGroupName;

		// Attributes
		static const FName MeshIdAttributeName;

		static const FName ParentsAttributeName;
		static const FName WeightsAttributeName;
		static const FName OffsetsAttributeName;

		/**
		* FSelectionFacade Constuctor
		* @param VertixDependencyGroup : GroupName the index attribute is dependent on. 
		*/
		FTetrahedralBindings(FManagedArrayCollection& InSelf);
		FTetrahedralBindings(const FManagedArrayCollection& InSelf);
		virtual ~FTetrahedralBindings();

		/** 
		* Create the facade schema. 
		*/
		void DefineSchema();

		/** Returns \c true if the facade is operating on a read-only geometry collection. */
		bool IsConst() const { return MeshIdAttribute.IsConst(); }

		/** 
		* Returns \c true if the Facade defined on the collection, and is initialized to
		* a valid bindings group.
		*/
		bool IsValid() const;

		/**
		* Given a \p MeshId (by convention \code Mesh->GetPrimaryAssetId() \endcode) and
		* a Level Of Detail rank, generate the associated bindings group name.
		*/
		static FName GenerateMeshGroupName(const int32 TetMeshIdx, const FName& MeshId, const int32 LOD);

		/**
		* Returns \c true if the specified bindings group exists.
		*/
		bool ContainsBindingsGroup(const int32 TetMeshIdx, const FName& MeshId, const int32 LOD) const;
		bool ContainsBindingsGroup(const FName& GroupName) const;

		/**
		* Create a new bindings group, allocating new arrays.
		*/
		void AddBindingsGroup(const int32 TetMeshIdx, const FName& MeshId, const int32 LOD);
		void AddBindingsGroup(const FName& GroupName);
		
		/**
		* Initialize local arrays to point at a bindings group associated with \p MeshId 
		* and \p LOD.  Returns \c false if it doesn't exist.
		*/
		bool ReadBindingsGroup(const int32 TetMeshIdx, const FName& MeshId, const int32 LOD);
		bool ReadBindingsGroup(const FName& GroupName);

		/**
		* Removes a group from the list of bindings groups, removes the bindings arrays 
		* from the geometry collection, and removes the group if it's empty.
		*/
		void RemoveBindingsGroup(const int32 TetMeshIdx, const FName& MeshId, const int32 LOD);
		void RemoveBindingsGroup(const FName& GroupName);

		/**
		* Authors bindings data.
		* 
		* \p Parents are indicies of vertices; tetrahedron or surface triangle where final 
		*    elem is \c INDEX_NONE.
		* \p Weights are barycentric coordinates.
		* \p Offsets are vectors from the barycentric point to the location, in the case 
		*    of a surface binding.
		*/
		void SetBindingsData(const TArray<FIntVector4>& Parents, const TArray<FVector4f>& Weights, const TArray<FVector3f>& Offsets);

		/**
		* Get Parents array.
		*/
		const TManagedArrayAccessor<FIntVector4>* GetParents() const { return Parents; }
		      TManagedArrayAccessor<FIntVector4>* GetParents() { check(!IsConst()); return Parents; }

		/**
		* Get Weights array.
		*/
		const TManagedArrayAccessor<FVector4f>* GetWeights() const { return Weights; }
		      TManagedArrayAccessor<FVector4f>* GetWeights() { check(!IsConst()); return Weights; }

		/**
		* Get Offsets array.
		*/
		const TManagedArrayAccessor<FVector3f>* GetOffsets() const { return Offsets; }
		      TManagedArrayAccessor<FVector3f>* GetOffsets() { check(!IsConst());  return Offsets; }

	private:
		TManagedArrayAccessor<FString> MeshIdAttribute;
		TManagedArrayAccessor<FIntVector4>* Parents;
		TManagedArrayAccessor<FVector4f>* Weights;
		TManagedArrayAccessor<FVector3f>* Offsets;
	};

}
