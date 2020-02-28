// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCacheAbcFileComponent.h"
#include "GeometryCache.h"
#include "GeometryCacheTrackAbcFile.h"
#include "GeometryCacheAbcFileSceneProxy.h"

#define LOCTEXT_NAMESPACE "GeometryCacheAbcFileComponent"

UGeometryCacheAbcFileComponent::UGeometryCacheAbcFileComponent()
{
	AbcSettings = NewObject<UAbcImportSettings>(this, TEXT("AbcSettings"));
	AbcSettings->ImportType = EAlembicImportType::GeometryCache;
}

#if WITH_EDITOR
void UGeometryCacheAbcFileComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FProperty* PropertyThatChanged = PropertyChangedEvent.MemberProperty;
	const FName PropertyName = PropertyThatChanged ? PropertyThatChanged->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UGeometryCacheAbcFileComponent, AlembicFilePath))
	{
		InitializeGeometryCache();
		InvalidateTrackSampleIndices();
	}

	UMeshComponent::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UGeometryCacheAbcFileComponent::ReloadAbcFile()
{
	if (!GeometryCache || GeometryCache->Tracks.Num() == 0 || AlembicFilePath.FilePath.IsEmpty())
	{
		return;
	}

	UGeometryCacheTrackAbcFile* AbcFileTrack = Cast<UGeometryCacheTrackAbcFile>(GeometryCache->Tracks[0]);

	AbcSettings->SamplingSettings = SamplingSettings;
	AbcSettings->MaterialSettings = MaterialSettings;
	AbcSettings->ConversionSettings = ConversionSettings;

	bool bIsValid = AbcFileTrack->SetSourceFile(AlembicFilePath.FilePath, AbcSettings);
	if (bIsValid)
	{
		// Also store the number of frames in the cache
		GeometryCache->SetFrameStartEnd(0, AbcFileTrack->GetEndFrameIndex());
	}

	MarkRenderStateDirty();
}

void UGeometryCacheAbcFileComponent::InitializeGeometryCache()
{
	UGeometryCacheTrackAbcFile* AbcFileTrack = nullptr;
	if (!GeometryCache)
	{
		// Transient GeometryCache for use in current session
		GeometryCache = NewObject<UGeometryCache>();
		AbcFileTrack = NewObject<UGeometryCacheTrackAbcFile>(GeometryCache);
		GeometryCache->AddTrack(AbcFileTrack);
	}
	else
	{
		AbcFileTrack = Cast<UGeometryCacheTrackAbcFile>(GeometryCache->Tracks[0]);
	}

	// #ueent_todo: Should be able to clear the Alembic file
	if (!AlembicFilePath.FilePath.IsEmpty() && (AlembicFilePath.FilePath != AbcFileTrack->GetSourceFile()))
	{
		ReloadAbcFile();
	}
}

void UGeometryCacheAbcFileComponent::PostLoad()
{
	Super::PostLoad();

	InitializeGeometryCache();
}

FPrimitiveSceneProxy* UGeometryCacheAbcFileComponent::CreateSceneProxy()
{
	return new FGeometryCacheAbcFileSceneProxy(this);
}

#undef LOCTEXT_NAMESPACE
