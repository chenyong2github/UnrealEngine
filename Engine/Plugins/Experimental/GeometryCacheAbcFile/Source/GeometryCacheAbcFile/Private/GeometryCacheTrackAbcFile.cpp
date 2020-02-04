// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCacheTrackAbcFile.h"

#include "AbcImporter.h"
#include "AbcUtilities.h"
#include "AbcImportSettings.h"
#include "GeometryCacheHelpers.h"

UGeometryCacheTrackAbcFile::UGeometryCacheTrackAbcFile()
: EndFrameIndex(0)
{
}

const bool UGeometryCacheTrackAbcFile::UpdateMeshData(const float Time, const bool bLooping, int32& InOutMeshSampleIndex, FGeometryCacheMeshData*& OutMeshData)
{
	const int32 SampleIndex = FindSampleIndexFromTime(Time, bLooping);

	// If InOutMeshSampleIndex equals -1 (first creation) update the OutVertices and InOutMeshSampleIndex
	// Update the Vertices and Index if SampleIndex is different from the stored InOutMeshSampleIndex
	if (InOutMeshSampleIndex == -1 || SampleIndex != InOutMeshSampleIndex)
	{
		if (GetMeshData(SampleIndex, MeshData))
		{
			OutMeshData = &MeshData;
			InOutMeshSampleIndex = SampleIndex;
			return true;
		}
	}
	return false;
}

const bool UGeometryCacheTrackAbcFile::UpdateBoundsData(const float Time, const bool bLooping, const bool bIsPlayingBackward, int32& InOutBoundsSampleIndex, FBox& OutBounds)
{
	const int32 SampleIndex = FindSampleIndexFromTime(Time, bLooping);

	FGeometryCacheTrackSampleInfo SampledInfo = GetSampleInfo(Time, bLooping);
	if (InOutBoundsSampleIndex != SampleIndex)
	{
		OutBounds = SampledInfo.BoundingBox;
		InOutBoundsSampleIndex = SampleIndex;
		return true;
	}
	return false;
}

bool UGeometryCacheTrackAbcFile::SetSourceFile(const FString& FilePath)
{
	if (!FilePath.IsEmpty())
	{
		AbcFile = MakeUnique<FAbcFile>(FilePath);
		EAbcImportError Result = AbcFile->Open();

		if (Result != EAbcImportError::AbcImportError_NoError)
		{
			AbcFile.Reset();
			return false;
		}

		// #ueent_todo: Expose some conversion settings in the component to use with the AbcFile
		UAbcImportSettings* ImportSettings = DuplicateObject(GetMutableDefault<UAbcImportSettings>(), GetTransientPackage());

		EndFrameIndex = FMath::Max(AbcFile->GetMaxFrameIndex() - 1, 1);
		ImportSettings->SamplingSettings.FrameEnd = EndFrameIndex;
		ImportSettings->ImportType = EAlembicImportType::GeometryCache;

		Result = AbcFile->Import(ImportSettings);

		if (Result != EAbcImportError::AbcImportError_NoError)
		{
			AbcFile.Reset();
			return false;
		}

		TArray<FMatrix> Mats;
		Mats.Add(FMatrix::Identity);
		Mats.Add(FMatrix::Identity);

		TArray<float> MatTimes;
		MatTimes.Add(0.0f);
		MatTimes.Add(AbcFile->GetImportLength() + AbcFile->GetImportTimeOffset());
		SetMatrixSamples(Mats, MatTimes);

		Duration = AbcFile->GetImportLength();

		// Fill out the MeshData with the sample from time 0
		GetMeshData(0, MeshData);
	}
	else
	{
		AbcFile.Reset();

		EndFrameIndex = 0;
		Duration = 0.f;

		MatrixSamples.Empty();
		MatrixSampleTimes.Empty();
	}
	SourceFile = FilePath;
	return true;
}

const int32 UGeometryCacheTrackAbcFile::FindSampleIndexFromTime(const float Time, const bool bLooping) const
{
	if (AbcFile)
	{
		float SampleTime = Time;
		if (bLooping)
		{
			SampleTime = GeometyCacheHelpers::WrapAnimationTime(Time, Duration);
		}
		return AbcFile->GetFrameIndex(SampleTime);
	}
	return 0;
}

const FGeometryCacheTrackSampleInfo& UGeometryCacheTrackAbcFile::GetSampleInfo(float Time, bool bLooping)
{
	float SampleTime = Time;
	if (bLooping)
	{
		SampleTime = GeometyCacheHelpers::WrapAnimationTime(Time, Duration);
	}

	// #ueent_todo: Return the correct SampleInfo without double querying the Alembic. 
	// Currently returns the info for MeshData queried in UpdateMeshData
	SampleInfo = FGeometryCacheTrackSampleInfo(
		SampleTime,
		MeshData.BoundingBox,
		MeshData.Positions.Num(),
		MeshData.Indices.Num()
	);

	return SampleInfo;
}

bool UGeometryCacheTrackAbcFile::GetMeshData(int32 SampleIndex, FGeometryCacheMeshData& OutMeshData)
{
	if (AbcFile.IsValid())
	{
		// #ueent_todo: Implement optimized Alembic querying
		FAbcUtilities::GetFrameMeshData(*AbcFile, SampleIndex, OutMeshData);
		return true;
	}
	return false;
}
