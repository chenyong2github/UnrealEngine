// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChaosClothAsset/ClothCollection.h"

namespace UE::Chaos::ClothAsset
{
	/**
	 * Geometry tools operating on cloth collections.
	 */
	struct CHAOSCLOTHASSET_API FClothGeometryTools
	{
		/** Delete the render mesh data. */
		static void DeleteRenderMesh(const TSharedPtr<FClothCollection>& ClothCollection);

		/** Turn the sim mesh portion of this ClothCollection into a render mesh. */
		static void CopySimMeshToRenderMesh(const TSharedPtr<FClothCollection>& ClothCollection, int32 MaterialIndex);

		/** Reverse the mesh normals. Will reverse all normals if pattern selection is empty. */
		static void ReverseNormals(
			const TSharedPtr<FClothCollection>& ClothCollection,
			bool bReverseSimMeshNormals,
			bool bReverseRenderMeshNormals,
			const TArray<int32>& PatternSelection);

		/** 
		 * Set the skinning weights for all of the sim/render vertices in ClothCollection to be bound to the root node. 
		 * 
		 * @param LODs if empty will apply the change to all LODs. Otherwise only LODs specified in the array (if exist) are affected. 
		 */
		static void BindMeshToRootBone(const TSharedPtr<FClothCollection>& ClothCollection,
									   bool bBindSimMesh,
									   bool bBindRenderMesh,
									   const TArray<int32> Lods = {});
	};
}  // End namespace UE::Chaos::ClothAsset