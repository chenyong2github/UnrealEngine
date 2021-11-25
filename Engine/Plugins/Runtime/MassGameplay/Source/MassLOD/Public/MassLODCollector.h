// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassLODLogic.h"
#include "DrawDebugHelpers.h"

/**
 * This is the expected member variables for the TArrayView<FMassLODSourceInfo> when calling TMassLODCollector methods
 *
struct FMassLODSourceInfo
{
	// Distance to viewer (Always needed)
	TStaticArray<float, UE::MassLOD::MaxNumOfViewers> DistanceToViewerSq;

	// Visibility information (Only when FLODLogic::bDoVisibilityLogic is enabled)
	TStaticArray<bool, UE::MassLOD::MaxNumOfViewers> bIsVisibleByViewer;
};
*/

/**
 * Helper struct to collect needed information on each agent that will be needed later for LOD calculation
 *   Requires FMassTransform fragment.
 *   Stores information in FMassLODSourceInfo fragment.
 */
template <typename FLODLogic = FLODDefaultLogic >
struct TMassLODCollector : public FMassLODBaseLogic
{
	/**
	 * Initializes the LOD collector, needed to be called once at initialization time
	 * @Param InDistanceToFrustum the extra distance to frustrum to use to know if the entity is visible (Only when FLODLogic::bDoVisibilityLogic is enabled)
	 * @Param InDistanceToFrustumHysteresis the hyteresis on the distance to frustrum to use when entity was previously visible to become not visible (Only when FLODLogic::bDoVisibilityLogic is enabled)
	 */
	void Initialize(const float InDistanceToFrustum = 0.0f, const float InDistanceToFrustumHysteresis = 0.0f, bool bTemp = false);

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
	template< typename FMassTransform, typename FMassLODSourceInfo >
	void CollectLODInfo(FMassExecutionContext& Context, TConstArrayView<FMassTransform> TranformList, TArrayView<FMassLODSourceInfo> ViewersInfoList);

protected:

	float DistanceToFrustum = 0.0f;
	float DistanceToFrustumHysteresis = 0.0f;
};

template <typename FLODLogic>
void TMassLODCollector<FLODLogic>::Initialize(const float InDistanceToFrustum /*= 0.0f*/, const float InDistanceToFrustumHysteresis /*= 0.0f*/, bool bTemp /*= false*/)
{
	DistanceToFrustum = InDistanceToFrustum;
	DistanceToFrustumHysteresis = InDistanceToFrustumHysteresis;
}

template <typename FLODLogic>
void TMassLODCollector<FLODLogic>::PrepareExecution(TConstArrayView<FViewerInfo> ViewersInfo)
{
	CacheViewerInformation(ViewersInfo, FLODLogic::bLocalViewersOnly);
}

template <typename FLODLogic>
template< typename FMassTransform, typename FMassLODSourceInfo >
void TMassLODCollector<FLODLogic>::CollectLODInfo(FMassExecutionContext& Context, TConstArrayView<FMassTransform> TranformList, TArrayView<FMassLODSourceInfo> ViewersInfoList)
{
	const int32 NumEntities = Context.GetNumEntities();
	for (int EntityIdx = 0; EntityIdx < NumEntities; EntityIdx++)
	{
		const FMassTransform& EntityTransform = TranformList[EntityIdx];
		FMassLODSourceInfo& EntityViewerInfo = ViewersInfoList[EntityIdx];
		for (int ViewerIdx = 0; ViewerIdx < Viewers.Num(); ++ViewerIdx)
		{
			const FViewerLODInfo& Viewer = Viewers[ViewerIdx];
			if (Viewer.bClearData)
			{
				EntityViewerInfo.DistanceToViewerSq[ViewerIdx] = FLT_MAX;
				SetbIsVisibleByViewer<FLODLogic::bDoVisibilityLogic>(EntityViewerInfo, ViewerIdx, false);
			}
			if (Viewer.Handle.IsValid())
			{
				const FVector& EntityLocation = EntityTransform.GetTransform().GetLocation();
				const FVector ViewerToEntity = EntityLocation - Viewer.Location;
				EntityViewerInfo.DistanceToViewerSq[ViewerIdx] = ViewerToEntity.SizeSquared();

				if (FLODLogic::bDoVisibilityLogic)
				{
					const bool bWasVisibleByViewer = GetbIsVisibleByViewer<FLODLogic::bDoVisibilityLogic>(EntityViewerInfo, ViewerIdx, false);
					bool bIsVisibleByViewer = false;

					if (EntityViewerInfo.DistanceToViewerSq[ViewerIdx] <= KINDA_SMALL_NUMBER)
					{
						bIsVisibleByViewer = true;
					}
					else
					{
						bIsVisibleByViewer = Viewer.Frustum.IntersectSphere(EntityLocation, bWasVisibleByViewer ? DistanceToFrustum + DistanceToFrustumHysteresis : DistanceToFrustum );
					}

					SetbIsVisibleByViewer<FLODLogic::bDoVisibilityLogic>(EntityViewerInfo, ViewerIdx, bIsVisibleByViewer);
				}
			}
		}
	}
}