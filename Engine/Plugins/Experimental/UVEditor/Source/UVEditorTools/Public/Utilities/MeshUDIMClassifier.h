// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"


namespace UE
{
	namespace Geometry
	{

		struct FUVOverlayView;
		class FMeshConnectedComponents;

		/**
		* FDynamicMeshUDIMClassifier is a utility class for identifying active UDIMs from a FDynamicMesh's UV overlay
		 */
		class UVEDITORTOOLS_API FDynamicMeshUDIMClassifier
		{
		public:
			explicit FDynamicMeshUDIMClassifier(const FDynamicMeshUVOverlay* UVOverlay);

			TArray<FVector2i> ActiveTiles() const;
			TArray<int32> TidsForTile(FVector2i TileIndexIn) const;

		protected:

			void ClassifyUDIMs();

			/** The UV Overlay to analyze for UDIMs */
			const FDynamicMeshUVOverlay* UVOverlay = nullptr;

			TMap<FVector2i, TArray<int32> > UDIMs;
		};
	}
}