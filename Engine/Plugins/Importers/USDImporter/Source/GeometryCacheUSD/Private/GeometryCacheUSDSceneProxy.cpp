// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCacheUSDSceneProxy.h"
#include "GeometryCacheUSDComponent.h"
#include "GeometryCacheTrackUSD.h"

FGeometryCacheUsdSceneProxy::FGeometryCacheUsdSceneProxy(UGeometryCacheUsdComponent* Component)
: FGeometryCacheSceneProxy(Component, [this]() { return new FGeomCacheTrackUsdProxy(GetScene().GetFeatureLevel()); })
{
}

bool FGeomCacheTrackUsdProxy::UpdateMeshData(float Time, bool bLooping, int32& InOutMeshSampleIndex, FGeometryCacheMeshData& OutMeshData)
{
	if (UGeometryCacheTrackUsd* UsdTrack = Cast<UGeometryCacheTrackUsd>(Track))
	{
		FGeometryCacheMeshData* PtrMeshData = nullptr;
		bool bResult = UsdTrack->UpdateMeshData(Time, bLooping, InOutMeshSampleIndex, PtrMeshData);
		if (bResult)
		{
			OutMeshData = *PtrMeshData;
		}
		return bResult;
	}
	return false;
}

bool FGeomCacheTrackUsdProxy::GetMeshData(int32 SampleIndex, FGeometryCacheMeshData& OutMeshData)
{
	if (UGeometryCacheTrackUsd* UsdTrack = Cast<UGeometryCacheTrackUsd>(Track))
	{
		return UsdTrack->GetMeshData(SampleIndex, OutMeshData);
	}
	return false;
}

bool FGeomCacheTrackUsdProxy::IsTopologyCompatible(int32 SampleIndexA, int32 SampleIndexB)
{
	// No support for interpolation for now (assume the topology is variable)
	return false;
}

const FVisibilitySample& FGeomCacheTrackUsdProxy::GetVisibilitySample(float Time, const bool bLooping) const
{
	// Assume the track is visible for its whole duration
	return FVisibilitySample::VisibleSample;
}

void FGeomCacheTrackUsdProxy::FindSampleIndexesFromTime(float Time, bool bLooping, bool bIsPlayingBackwards, int32 &OutFrameIndex, int32 &OutNextFrameIndex, float &InInterpolationFactor)
{
	if (UGeometryCacheTrackUsd* UsdTrack = Cast<UGeometryCacheTrackUsd>(Track))
	{
		int32 ThisFrameIndex = UsdTrack->FindSampleIndexFromTime(Time, bLooping);
		int32 LastFrameIndex = UsdTrack->GetEndFrameIndex();
		OutFrameIndex = ThisFrameIndex;
		OutNextFrameIndex = OutFrameIndex + 1;
		InInterpolationFactor = 0.f;

		// If playing backwards the logical order of previous and next is reversed
		if (bIsPlayingBackwards)
		{
			Swap(OutFrameIndex, OutNextFrameIndex);
			InInterpolationFactor = 1.0f - InInterpolationFactor;
		}
	}
}
