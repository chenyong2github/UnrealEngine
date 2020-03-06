// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCacheTrackAbcFile.h"

#include "AbcImporter.h"
#include "AbcImportLogger.h"
#include "AbcImportSettings.h"
#include "AbcUtilities.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GeometryCache.h"
#include "GeometryCacheHelpers.h"
#include "Logging/LogCategory.h"
#include "Logging/LogMacros.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "Widgets/Notifications/SNotificationList.h"

DEFINE_LOG_CATEGORY_STATIC(LogGeometryCacheAbcFile, Log, All);

#define LOCTEXT_NAMESPACE "GeometryCacheTrackAbcFile"

UGeometryCacheTrackAbcFile::UGeometryCacheTrackAbcFile()
: EndFrameIndex(0)
{
}

const bool UGeometryCacheTrackAbcFile::UpdateMatrixData(const float Time, const bool bLooping, int32& InOutMatrixSampleIndex, FMatrix& OutWorldMatrix)
{
	if (AbcFile)
	{
		return Super::UpdateMatrixData(Time, bLooping, InOutMatrixSampleIndex, OutWorldMatrix);
	}
	return false;
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

void UGeometryCacheTrackAbcFile::Reset()
{
	AbcFile.Reset();

	EndFrameIndex = 0;
	Duration = 0.f;

	MatrixSamples.Reset();
	MatrixSampleTimes.Reset();

	MeshData = FGeometryCacheMeshData();
	MeshData.BoundingBox = FBox(ForceInit);
}

void UGeometryCacheTrackAbcFile::ShowNotification(const FText& Text)
{
	FNotificationInfo Info(Text);
	Info.bFireAndForget = true;
	Info.bUseLargeFont = false;
	Info.FadeOutDuration = 3.0f;
	Info.ExpireDuration = 7.0f;

	FSlateNotificationManager::Get().AddNotification(Info);
}

bool UGeometryCacheTrackAbcFile::SetSourceFile(const FString& FilePath, UAbcImportSettings* AbcSettings)
{
	if (!FilePath.IsEmpty())
	{
		AbcFile = MakeUnique<FAbcFile>(FilePath);
		EAbcImportError Result = AbcFile->Open();

		const FString Filename = FPaths::GetCleanFilename(FilePath);

		if (Result != EAbcImportError::AbcImportError_NoError)
		{
			Reset();

			FText FailureMessage = LOCTEXT("OpenFailureReason_Unknown", "Unknown open failure");
			switch (Result)
			{
			case EAbcImportError::AbcImportError_InvalidArchive:
				FailureMessage = LOCTEXT("OpenFailureReason_InvalidArchive", "Not a valid Alembic file");
				break;
			case EAbcImportError::AbcImportError_NoValidTopObject:
				FailureMessage = LOCTEXT("OpenFailureReason_InvalidRoot", "Alembic file has no valid root node");
				break;
			}
			UE_LOG(LogGeometryCacheAbcFile, Warning, TEXT("Failed to open %s: %s"), *Filename, *FailureMessage.ToString());

			return false;
		}

		// Set the end frame from the Abc if none is specified in the settings
		EndFrameIndex = FMath::Max(AbcFile->GetMaxFrameIndex() - 1, 1);
		if (AbcSettings->SamplingSettings.FrameEnd == 0)
		{
			AbcSettings->SamplingSettings.FrameEnd = EndFrameIndex;
		}

		Result = AbcFile->Import(AbcSettings);

		if (Result != EAbcImportError::AbcImportError_NoError)
		{
			Reset();

			FText FailureMessage = LOCTEXT("LoadFailureReason_Unknown", "Unknown load failure");
			TArray<TSharedRef<FTokenizedMessage>> Messages = FAbcImportLogger::RetrieveMessages();
			if (Messages.Num() > 0)
			{
				FailureMessage = Messages[0]->ToText();
			}
			UE_LOG(LogGeometryCacheAbcFile, Warning, TEXT("Failed to load %s: %s"), *Filename, *FailureMessage.ToString());

			ShowNotification(FText::Format(LOCTEXT("LoadErrorNotification", "{0} could not be loaded. See Output Log for details."), FText::FromString(Filename)));

			return false;
		}

		MatrixSamples.Reset();
		MatrixSampleTimes.Reset();

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

		if (MeshData.Positions.Num() == 0)
		{
			// This could happen if the Alembic has geometry but they are set as invisible in the source
			ShowNotification(FText::Format(LOCTEXT("NoVisibleGeometry", "Warning: {0} has no visible geometry."), FText::FromString(Filename)));
		}
	}
	else
	{
		Reset();
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
	if (AbcFile)
	{
		// #ueent_todo: Implement optimized Alembic querying
		FAbcUtilities::GetFrameMeshData(*AbcFile, SampleIndex, OutMeshData);
		return true;
	}
	return false;
}

void UGeometryCacheTrackAbcFile::SetupGeometryCacheMaterials(UGeometryCache* GeometryCache)
{
	if (AbcFile)
	{
		// Create package where the materials will be saved into
		static const FString DestinationPath(TEXT("/Game/GeometryCacheAbcFile/Materials"));
		FString Name = FPaths::GetBaseFilename(SourceFile);
		FString PackageName = UPackageTools::SanitizePackageName(FPaths::Combine(*DestinationPath, *Name, *Name));

		UPackage* Package = CreatePackage(nullptr, *PackageName);
		Package->FullyLoad();

		FAbcUtilities::SetupGeometryCacheMaterials(*AbcFile, GeometryCache, Package);
	}
}

#undef LOCTEXT_NAMESPACE