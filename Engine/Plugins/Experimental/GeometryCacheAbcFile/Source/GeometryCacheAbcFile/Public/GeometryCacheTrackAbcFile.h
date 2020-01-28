// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbcFile.h"
#include "GeometryCacheTrack.h"
#include "GeometryCacheMeshData.h"

#include "GeometryCacheTrackAbcFile.generated.h"

/** GeometryCacheTrack for Alembic file querying */
UCLASS(collapsecategories, hidecategories = Object, BlueprintType, config = Engine)
class GEOMETRYCACHEABCFILE_API UGeometryCacheTrackAbcFile : public UGeometryCacheTrack
{
	GENERATED_BODY()

	UGeometryCacheTrackAbcFile();

public:

	//~ Begin UGeometryCacheTrack Interface.
	virtual const bool UpdateMeshData(const float Time, const bool bLooping, int32& InOutMeshSampleIndex, FGeometryCacheMeshData*& OutMeshData) override;
	virtual const bool UpdateBoundsData(const float Time, const bool bLooping, const bool bIsPlayingBackward, int32& InOutBoundsSampleIndex, FBox& OutBounds) override;
	virtual const FGeometryCacheTrackSampleInfo& GetSampleInfo(float Time, const bool bLooping) override;
	//~ End UGeometryCacheTrack Interface.

	bool SetSourceFile(const FString& FilePath);
	const FString& GetSourceFile() const { return SourceFile; }

	const int32 FindSampleIndexFromTime(const float Time, const bool bLooping) const;

	int32 GetEndFrameIndex() const { return EndFrameIndex;  }

	bool GetMeshData(int32 SampleIndex, FGeometryCacheMeshData& OutMeshData);

private:
	FGeometryCacheMeshData MeshData;
	FGeometryCacheTrackSampleInfo SampleInfo;
	TUniquePtr<FAbcFile> AbcFile;
	FString SourceFile;
	int32 EndFrameIndex;
};
