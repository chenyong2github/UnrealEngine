// Copyright Epic Games, Inc. All Rights Reserved.


#include "HairStrandsCluster.h"
#include "HairStrandsUtils.h"
#include "SceneRendering.h"
#include "SceneManagement.h"

FHairStrandsClusterViews CreateHairStrandsClusters(
	FRHICommandListImmediate& RHICmdList,
	const FScene* Scene,
	const TArray<FViewInfo>& Views)
{
	FHairStrandsClusterViews PrimitivesClusterViews;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		if (View.Family)
		{
			int32 MaterialId = 0;
			FHairStrandsClusterDatas& PrimitivesClusters = PrimitivesClusterViews.Views.AddDefaulted_GetRef();

			if (View.HairStrandsMeshElements.Num() == 0)
			{
				continue;
			}

			// Spatially clusters all hair primitives within the same area, and render a single DOM for them
			uint32 ClusterId = 0;
			auto UpdateCluster = [&PrimitivesClusters, &View, &ClusterId, &MaterialId](const FMeshBatchAndRelevance* MeshBatchAndRelevance, const FPrimitiveSceneProxy* Proxy)
			{
				const FBoxSphereBounds& PrimitiveBounds = Proxy->GetBounds();

				bool bClusterFound = false;
				for (FHairStrandsClusterData& Cluster : PrimitivesClusters.Datas)
				{
					const bool bIntersect = FBoxSphereBounds::SpheresIntersect(Cluster.Bounds, PrimitiveBounds);
					if (bIntersect)
					{
						Cluster.Bounds = Union(Cluster.Bounds, PrimitiveBounds);

						if (MeshBatchAndRelevance)
						{
							FHairStrandsClusterData::PrimitiveInfo& PrimitiveInfo = Cluster.PrimitivesInfos.AddZeroed_GetRef();
							PrimitiveInfo.MeshBatchAndRelevance = *MeshBatchAndRelevance;
							PrimitiveInfo.MaterialId = MaterialId++;
						}
						bClusterFound = true;
						break;
					}
				}

				if (!bClusterFound)
				{
					FHairStrandsClusterData Cluster;
					Cluster.ClusterId = ClusterId++;
					if (MeshBatchAndRelevance)
					{
						FHairStrandsClusterData::PrimitiveInfo& PrimitiveInfo = Cluster.PrimitivesInfos.AddZeroed_GetRef();
						PrimitiveInfo.MeshBatchAndRelevance = *MeshBatchAndRelevance;
						PrimitiveInfo.MaterialId = MaterialId++;
					}
					Cluster.Bounds = PrimitiveBounds;
					PrimitivesClusters.Datas.Add(Cluster);
				}
			};

			for (const FMeshBatchAndRelevance& MeshBatchAndRelevance : View.HairStrandsMeshElements)
			{
				UpdateCluster(&MeshBatchAndRelevance, MeshBatchAndRelevance.PrimitiveSceneProxy);
			}

			for (FHairStrandsClusterData& Cluster : PrimitivesClusters.Datas)
			{
				Cluster.ScreenRect = ComputeProjectedScreenRect(Cluster.Bounds.GetBox(), View);
			}
		}
	}

	return PrimitivesClusterViews;
}
