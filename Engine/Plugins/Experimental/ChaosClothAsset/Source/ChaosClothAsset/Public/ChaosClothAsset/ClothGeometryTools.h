// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

struct FManagedArrayCollection;
namespace UE::Geometry { class FDynamicMesh3; }

namespace UE::Chaos::ClothAsset
{
	/**
	 * Geometry tools operating on cloth collections.
	 */
	struct CHAOSCLOTHASSET_API FClothGeometryTools
	{

		/** Return whether at least one pattern of this collection has any faces to simulate. */
		static bool HasSimMesh(const TSharedPtr<const FManagedArrayCollection>& ClothCollection);

		/** Return whether at least one pattern of this collection has any faces to render. */
		static bool HasRenderMesh(const TSharedPtr<const FManagedArrayCollection>& ClothCollection);

		/** Delete the render mesh data. */
		static void DeleteRenderMesh(const TSharedPtr<FManagedArrayCollection>& ClothCollection);

		/** Delete the sim mesh data. */
		static void DeleteSimMesh(const TSharedPtr<FManagedArrayCollection>& ClothCollection);

		/** Remove all tethers. */
		static void DeleteTethers(const TSharedPtr<FManagedArrayCollection>& ClothCollection);

		/** Turn the sim mesh portion of this ClothCollection into a render mesh. */
		static void CopySimMeshToRenderMesh(const TSharedPtr<FManagedArrayCollection>& ClothCollection, const FString& RenderMaterialPathName, bool bSingleRenderPattern);

		/** Reverse the mesh normals. Will reverse all normals if pattern selection is empty. */
		static void ReverseMesh(
			const TSharedPtr<FManagedArrayCollection>& ClothCollection,
			bool bReverseSimMeshNormals,
			bool bReverseSimMeshWindingOrder,
			bool bReverseRenderMeshNormals,
			bool bReverseRenderMeshWindingOrder,
			const TArray<int32>& SimPatternSelection,
			const TArray<int32>& RenderPatternSelection);

		/**
		 * Set the skinning weights for all of the sim/render vertices in ClothCollection to be bound to the root node.
		 *
		 * @param Lods if empty will apply the change to all LODs. Otherwise only LODs specified in the array (if exist) are affected.
		 */
		static void BindMeshToRootBone(
			const TSharedPtr<FManagedArrayCollection>& ClothCollection,
			bool bBindSimMesh,
			bool bBindRenderMesh);

		/**
		* Unwrap and build SimMesh data from a DynamicMesh
		*/
		static void BuildSimMeshFromDynamicMesh(
			const TSharedPtr<FManagedArrayCollection>& ClothCollection,
			const UE::Geometry::FDynamicMesh3& DynamicMesh, int32 UVChannelIndex, const FVector2f& UVScale, bool bAppend);
	};
}  // End namespace UE::Chaos::ClothAsset