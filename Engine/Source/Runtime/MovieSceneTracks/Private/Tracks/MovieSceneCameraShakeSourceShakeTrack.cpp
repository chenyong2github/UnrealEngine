// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tracks/MovieSceneCameraShakeSourceShakeTrack.h"
#include "Sections/MovieSceneCameraShakeSourceShakeSection.h"
#include "Compilation/MovieSceneCompilerRules.h"

#define LOCTEXT_NAMESPACE "MovieSceneCameraShakeSourceShakeTrack"

UMovieSceneSection* UMovieSceneCameraShakeSourceShakeTrack::AddNewCameraShake(const FFrameNumber KeyTime, const TSubclassOf<UCameraShake> ShakeClass)
{
	Modify();

	UMovieSceneCameraShakeSourceShakeSection* const NewSection = Cast<UMovieSceneCameraShakeSourceShakeSection>(CreateNewSection());
	if (NewSection)
	{
		// #fixme get length
		FFrameTime Duration = 5.0 * GetTypedOuter<UMovieScene>()->GetTickResolution();
		NewSection->InitialPlacement(CameraShakeSections, KeyTime, Duration.FrameNumber.Value, SupportsMultipleRows());
		NewSection->ShakeData.ShakeClass = ShakeClass;
		
		AddSection(*NewSection);
	}

	return NewSection;
}

FMovieSceneTrackSegmentBlenderPtr UMovieSceneCameraShakeSourceShakeTrack::GetTrackSegmentBlender() const
{
	return FMovieSceneAdditiveCameraTrackBlender();
}

#if WITH_EDITORONLY_DATA
FText UMovieSceneCameraShakeSourceShakeTrack::GetDisplayName() const
{
	return LOCTEXT("TrackName", "Camera Shake");
}
#endif

const TArray<UMovieSceneSection*>& UMovieSceneCameraShakeSourceShakeTrack::GetAllSections() const
{
	return CameraShakeSections;
}

bool UMovieSceneCameraShakeSourceShakeTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UMovieSceneCameraShakeSourceShakeSection::StaticClass();
}

UMovieSceneSection* UMovieSceneCameraShakeSourceShakeTrack::CreateNewSection()
{
	return NewObject<UMovieSceneCameraShakeSourceShakeSection>(this, NAME_None, RF_Transactional);
}

void UMovieSceneCameraShakeSourceShakeTrack::RemoveAllAnimationData()
{
	CameraShakeSections.Empty();
}

bool UMovieSceneCameraShakeSourceShakeTrack::HasSection(const UMovieSceneSection& Section) const
{
	return CameraShakeSections.Contains(&Section);
}

void UMovieSceneCameraShakeSourceShakeTrack::AddSection(UMovieSceneSection& Section)
{
	CameraShakeSections.Add(&Section);
}

void UMovieSceneCameraShakeSourceShakeTrack::RemoveSection(UMovieSceneSection& Section)
{
	CameraShakeSections.Remove(&Section);
}

void UMovieSceneCameraShakeSourceShakeTrack::RemoveSectionAt(int32 SectionIndex)
{
	CameraShakeSections.RemoveAt(SectionIndex);
}

bool UMovieSceneCameraShakeSourceShakeTrack::IsEmpty() const
{
	return CameraShakeSections.Num() == 0;
}

#undef LOCTEXT_NAMESPACE

