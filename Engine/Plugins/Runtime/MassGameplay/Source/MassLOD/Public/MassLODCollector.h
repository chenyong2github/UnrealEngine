// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassLODLogic.h"
#include "DrawDebugHelpers.h"



/**
 * Helper struct to collect needed information on each agent that will be needed later for LOD calculation
 *   Requires TTransformFragment fragment.
 *   Stores information in TViewerInfoFragment fragment.
 */
template <typename FLODLogic = FLODDefaultLogic >
struct TMassLODCollector : public FMassLODBaseLogic
{
	/**
	 * Prepares execution for the current frame, needed to be called before every execution
	 * @Param Viewers is the array of all the known viewers
	 */
	void PrepareExecution(TConstArrayView<FViewerInfo> Viewers);

	/**
	 * Collects the information for LOD calculation, called for each entity chunks
	 * @Param Context of the chunk execution
	 * @Param TranformList is the fragment transforms of the entities
	 * @Param ViewersInfoList is the fragment where to store source information for LOD calculation
	 */
	template <typename TTransformFragment, typename TViewerInfoFragment>
	void CollectLODInfo(FMassExecutionContext& Context, TConstArrayView<TTransformFragment> TranformList, TArrayView<TViewerInfoFragment> ViewersInfoList);

};

template <typename FLODLogic>
void TMassLODCollector<FLODLogic>::PrepareExecution(TConstArrayView<FViewerInfo> ViewersInfo)
{
	CacheViewerInformation(ViewersInfo, FLODLogic::bLocalViewersOnly);
}

template <typename FLODLogic>
template <typename TTransformFragment, typename TViewerInfoFragment>
void TMassLODCollector<FLODLogic>::CollectLODInfo(FMassExecutionContext& Context, TConstArrayView<TTransformFragment> TranformList, TArrayView<TViewerInfoFragment> ViewersInfoList)
{
	const int32 NumEntities = Context.GetNumEntities();
	for (int EntityIdx = 0; EntityIdx < NumEntities; EntityIdx++)
	{
		float ClosestViewerDistanceSq = FLT_MAX;
		float ClosestDistanceToFrustum = FLT_MAX;
		const TTransformFragment& EntityTransform = TranformList[EntityIdx];
		TViewerInfoFragment& EntityViewerInfo = ViewersInfoList[EntityIdx];

		SetDistanceToViewerSqNum<FLODLogic::bStoreInfoPerViewer>(EntityViewerInfo, Viewers.Num());
		SetDistanceToFrustumNum<FLODLogic::bDoVisibilityLogic && FLODLogic::bStoreInfoPerViewer>(EntityViewerInfo, Viewers.Num());
		for (int ViewerIdx = 0; ViewerIdx < Viewers.Num(); ++ViewerIdx)
		{
			const FViewerLODInfo& Viewer = Viewers[ViewerIdx];
			if (Viewer.bClearData)
			{
				SetDistanceToViewerSq<FLODLogic::bStoreInfoPerViewer>(EntityViewerInfo, ViewerIdx, FLT_MAX);
				SetDistanceToFrustum<FLODLogic::bDoVisibilityLogic && FLODLogic::bStoreInfoPerViewer>(EntityViewerInfo, ViewerIdx, FLT_MAX);
			}
			if (Viewer.Handle.IsValid())
			{
				const FVector& EntityLocation = EntityTransform.GetTransform().GetLocation();
				const FVector ViewerToEntity = EntityLocation - Viewer.Location;
				const float DistanceToViewerSq = ViewerToEntity.SizeSquared();
				if (ClosestViewerDistanceSq > DistanceToViewerSq)
				{
					ClosestViewerDistanceSq = DistanceToViewerSq;
				}
				SetDistanceToViewerSq<FLODLogic::bStoreInfoPerViewer>(EntityViewerInfo, ViewerIdx, DistanceToViewerSq);

				if (FLODLogic::bDoVisibilityLogic)
				{
					const float DistanceToFrustum = Viewer.Frustum.DistanceTo(EntityLocation);
					SetDistanceToFrustum<FLODLogic::bDoVisibilityLogic && FLODLogic::bStoreInfoPerViewer>(EntityViewerInfo, ViewerIdx, DistanceToFrustum);
					if (ClosestDistanceToFrustum > DistanceToFrustum)
					{
						ClosestDistanceToFrustum = DistanceToFrustum;
					}

				}
			}
		}
		EntityViewerInfo.ClosestViewerDistanceSq = ClosestViewerDistanceSq;
		SetClosestDistanceToFrustum<FLODLogic::bDoVisibilityLogic>(EntityViewerInfo, ClosestDistanceToFrustum);
	}
}