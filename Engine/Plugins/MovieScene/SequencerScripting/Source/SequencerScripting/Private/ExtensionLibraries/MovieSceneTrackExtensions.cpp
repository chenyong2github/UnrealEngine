// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExtensionLibraries/MovieSceneTrackExtensions.h"
#include "MovieSceneNameableTrack.h"
#include "MovieSceneTrack.h"

void UMovieSceneTrackExtensions::SetDisplayName(UMovieSceneTrack* Track, const FText& InName)
{
	if (UMovieSceneNameableTrack* NameableTrack = Cast<UMovieSceneNameableTrack>(Track))
	{
#if WITH_EDITORONLY_DATA
		NameableTrack->SetDisplayName(InName);
#endif
	}
}

FText UMovieSceneTrackExtensions::GetDisplayName(UMovieSceneTrack* Track)
{
#if WITH_EDITORONLY_DATA
	return Track->GetDisplayName();
#endif
	return FText::GetEmpty();
}

UMovieSceneSection* UMovieSceneTrackExtensions::AddSection(UMovieSceneTrack* Track)
{
	UMovieSceneSection* NewSection = Track->CreateNewSection();

	if (NewSection)
	{
		Track->Modify();

		Track->AddSection(*NewSection);
	}

	return NewSection;
}

TArray<UMovieSceneSection*> UMovieSceneTrackExtensions::GetSections(UMovieSceneTrack* Track)
{
	return Track->GetAllSections();
}

void UMovieSceneTrackExtensions::RemoveSection(UMovieSceneTrack* Track, UMovieSceneSection* Section)
{
	if (Section)
	{
		Track->Modify();

		Track->RemoveSection(*Section);
	}
}

int32 UMovieSceneTrackExtensions::GetSortingOrder(UMovieSceneTrack* Track) 
{ 
#if WITH_EDITORONLY_DATA
	return Track->GetSortingOrder(); 
#endif
	return 0;
}
 
void UMovieSceneTrackExtensions::SetSortingOrder(UMovieSceneTrack* Track, int32 SortingOrder) 
{
#if WITH_EDITORONLY_DATA
	Track->SetSortingOrder(SortingOrder); 
#endif
}

FColor UMovieSceneTrackExtensions::GetColorTint(UMovieSceneTrack* Track) 
{ 
#if WITH_EDITORONLY_DATA
	return Track->GetColorTint(); 
#endif
	return FColor();
}

void UMovieSceneTrackExtensions::SetColorTint(UMovieSceneTrack* Track, const FColor& ColorTint) 
{ 
#if WITH_EDITORONLY_DATA
	Track->SetColorTint(ColorTint); 
#endif
}

UMovieSceneSection* UMovieSceneTrackExtensions::GetSectionToKey(UMovieSceneTrack* Track) 
{ 
	return Track->GetSectionToKey(); 
}

void UMovieSceneTrackExtensions::SetSectionToKey(UMovieSceneTrack* Track, UMovieSceneSection* Section) 
{ 
	Track->SetSectionToKey(Section); 
}


