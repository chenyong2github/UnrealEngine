// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GeometryCacheTrack.h"
#include "GeometryCacheMeshData.h"

#include "GeometryCacheTrackUSD.generated.h"

typedef TFunction< bool(FGeometryCacheMeshData&, const FString&, float Time) > FReadUsdMeshFunction;

/** GeometryCacheTrack for querying USD */
UCLASS(collapsecategories, hidecategories = Object, BlueprintType, config = Engine)
class GEOMETRYCACHEUSD_API UGeometryCacheTrackUsd : public UGeometryCacheTrack
{
	GENERATED_BODY()

	UGeometryCacheTrackUsd();
	virtual ~UGeometryCacheTrackUsd();

public:

	//~ Begin UGeometryCacheTrack Interface.
	virtual const bool UpdateMeshData(const float Time, const bool bLooping, int32& InOutMeshSampleIndex, FGeometryCacheMeshData*& OutMeshData) override;
	virtual const bool UpdateBoundsData(const float Time, const bool bLooping, const bool bIsPlayingBackward, int32& InOutBoundsSampleIndex, FBox& OutBounds) override;
	virtual const FGeometryCacheTrackSampleInfo& GetSampleInfo(float Time, const bool bLooping) override;
	//~ End UGeometryCacheTrack Interface.

	void Initialize(FReadUsdMeshFunction InReadFunc, const FString& InPrimpath, int32 InStartFrameIndex, int32 InEndFrameIndex);

	const int32 FindSampleIndexFromTime(const float Time, const bool bLooping) const;

	int32 GetStartFrameIndex() const { return StartFrameIndex; }
	int32 GetEndFrameIndex() const { return EndFrameIndex;  }

	bool GetMeshData(int32 SampleIndex, FGeometryCacheMeshData& OutMeshData);

private:
	FGeometryCacheMeshData MeshData;
	TArray<FGeometryCacheTrackSampleInfo> SampleInfos;

	int32 StartFrameIndex;
	int32 EndFrameIndex;
};
