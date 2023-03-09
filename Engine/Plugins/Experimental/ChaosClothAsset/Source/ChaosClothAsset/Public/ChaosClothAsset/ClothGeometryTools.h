// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

struct FManagedArrayCollection;

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

		/** Set the skeleton path name on each LOD of the specified collection. */
		static void SetSkeletonAssetPathName(const TSharedPtr<FManagedArrayCollection>& ClothCollection, const FString& SkeletonAssetPathName);

		/** Set the physics path name on each LOD of the specified collection. */
		static void SetPhysicsAssetPathName(const TSharedPtr<FManagedArrayCollection>& ClothCollection, const FString& PhysicsAssetPathName);

		/** Delete the render mesh data. */
		static void DeleteRenderMesh(const TSharedPtr<FManagedArrayCollection>& ClothCollection);

		/** Turn the sim mesh portion of this ClothCollection into a render mesh. */
		static void CopySimMeshToRenderMesh(const TSharedPtr<FManagedArrayCollection>& ClothCollection, const FString& RenderMaterialPathName);

		/** Reverse the mesh normals. Will reverse all normals if pattern selection is empty. */
		static void ReverseNormals(
			const TSharedPtr<FManagedArrayCollection>& ClothCollection,
			bool bReverseSimMeshNormals,
			bool bReverseRenderMeshNormals,
			const TArray<int32>& PatternSelection);

		/**
		 * Set the skinning weights for all of the sim/render vertices in ClothCollection to be bound to the root node.
		 *
		 * @param Lods if empty will apply the change to all LODs. Otherwise only LODs specified in the array (if exist) are affected.
		 */
		static void BindMeshToRootBone(
			const TSharedPtr<FManagedArrayCollection>& ClothCollection,
			bool bBindSimMesh,
			bool bBindRenderMesh,
			const TArray<int32> Lods = {});
	};
}  // End namespace UE::Chaos::ClothAsset