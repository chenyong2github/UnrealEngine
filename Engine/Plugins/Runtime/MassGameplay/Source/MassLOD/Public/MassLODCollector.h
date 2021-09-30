// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassLODLogic.h"
#include "DrawDebugHelpers.h"

/**
 * This is the expected member variables for the TArrayView<FMassLODSourceInfo> when calling TMassLODCollector methods
 */
struct FMassLODSourceInfo
{
	// Distance to viewer (Always needed)
	TStaticArray<float, UE::MassLOD::MaxNumOfViewers> DistanceToViewerSq;

	// Visibility information (Only when FLODLogic::bDoVisibilityLogic is enabled)
	TStaticArray<bool, UE::MassLOD::MaxNumOfViewers> bIsVisibleByViewer;
};

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
	 * @Param InFOVAngleToDriveVisibility the FOV angle that will be use for visibility check in degrees (Only when FLODLogic::bDoVisibilityLogic is enabled)
	 * @Param InBufferHysteresisOnFOVRatio the FOV angle hysteresis that will be use for visibility check (Only when FLODLogic::bDoVisibilityLogic is enabled)
	 */
	void Initialize(const float InFOVAngleToDriveVisibility = -1.0f, const float InBufferHysteresisOnFOVRatio = -1.0f);

	/**
	 * Prepares execution for the current frame, needed to be called before every execution
	 * @Param Viewers is the array of all the known viewers
	 */
	void PrepareExecution(TConstArrayView<FViewerInfo> Viewers);

	/**
	 * Collects the information for LOD calculation, called for each entity chunks
	 * @Param Context of the chunk execution
	 * @Param LocationList is the fragment transforms of the entities
	 * @Param ViewersInfoList is the fragment where to store source information for LOD calculation
	 */
	template< typename FMassTransform, typename FMassLODSourceInfo >
	void CollectLODInfo(FMassExecutionContext& Context, TConstArrayView<FMassTransform> LocationList, TArrayView<FMassLODSourceInfo> ViewersInfoList);

protected:

	float FOVAngleToDriveVisibilityRad = 0.0f;
	float CosFOVAngleToDriveVisibility = 0.0f;
	float BufferHysteresisOnFOVRatio = 0.1f;
};

template <typename FLODLogic>
void TMassLODCollector<FLODLogic>::Initialize(const float InFOVAngleToDriveVisibility/*= -1.0f*/, const float InBufferHysteresisOnFOVRatio /*= -1.0f*/)
{
	checkf(!FLODLogic::bDoVisibilityLogic || (InFOVAngleToDriveVisibility > 0.0f && BufferHysteresisOnFOVRatio > 0.0f), TEXT("Expecting FOVAngleToDriveVisibility and BufferHysteresisOnFOVRatio when bDoVisibilityLogic is true"));
	FOVAngleToDriveVisibilityRad = FMath::DegreesToRadians(InFOVAngleToDriveVisibility);
	CosFOVAngleToDriveVisibility = FMath::Cos(FOVAngleToDriveVisibilityRad);
	BufferHysteresisOnFOVRatio = InBufferHysteresisOnFOVRatio;
}

template <typename FLODLogic>
void TMassLODCollector<FLODLogic>::PrepareExecution(TConstArrayView<FViewerInfo> Viewers)
{
	CacheViewerInformation(Viewers, FLODLogic::bLocalViewersOnly);
}

template <typename FLODLogic>
template< typename FMassTransform, typename FMassLODSourceInfo >
void TMassLODCollector<FLODLogic>::CollectLODInfo(FMassExecutionContext& Context, TConstArrayView<FMassTransform> LocationList, TArrayView<FMassLODSourceInfo> ViewersInfoList)
{
	const int32 NumEntities = Context.GetEntitiesNum();
	for (int EntityIdx = 0; EntityIdx < NumEntities; EntityIdx++)
	{
		const FMassTransform& EntityLocation = LocationList[EntityIdx];
		FMassLODSourceInfo& EntityViewerInfo = ViewersInfoList[EntityIdx];
		for (int ViewerIdx = 0; ViewerIdx < NumOfViewers; ++ViewerIdx)
		{
			if (bClearViewerData[ViewerIdx])
			{
				EntityViewerInfo.DistanceToViewerSq[ViewerIdx] = FLT_MAX;
				SetbIsVisibleByViewer<FLODLogic::bDoVisibilityLogic>(EntityViewerInfo, ViewerIdx, false);
			}
			if (ViewersHandles[ViewerIdx].IsValid())
			{
				const FVector ViewerToEntity = EntityLocation.GetTransform().GetLocation() - ViewersLocation[ViewerIdx];
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
						const FVector ViewerToEntityNorm = ViewerToEntity * FMath::InvSqrt(EntityViewerInfo.DistanceToViewerSq[ViewerIdx]);
						const float Dot = ViewersDirection[ViewerIdx] | ViewerToEntityNorm;

						// If we were visible and are now exiting defined FOV angle, consider a buffer hysteresis to give some room before toggling off visibility to prevent oscillating LOD states
						if (bWasVisibleByViewer && Dot < CosFOVAngleToDriveVisibility)
						{
							const float FOVToCos = FMath::Cos(FMath::Clamp(FOVAngleToDriveVisibilityRad * (1.f + BufferHysteresisOnFOVRatio), 0.f, PI));
							bIsVisibleByViewer = Dot > FOVToCos;
						}
						else
						{
							bIsVisibleByViewer = Dot >= CosFOVAngleToDriveVisibility;
						}
					}

					SetbIsVisibleByViewer<FLODLogic::bDoVisibilityLogic>(EntityViewerInfo, ViewerIdx, bIsVisibleByViewer);
				}
			}
		}
	}
}