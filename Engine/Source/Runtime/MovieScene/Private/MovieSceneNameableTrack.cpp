// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneNameableTrack.h"
#include "UObject/NameTypes.h"

#define LOCTEXT_NAMESPACE "MovieSceneNameableTrack"


/* UMovieSceneNameableTrack interface
 *****************************************************************************/

#if WITH_EDITORONLY_DATA

void UMovieSceneNameableTrack::SetDisplayName(const FText& NewDisplayName)
{
	if (NewDisplayName.EqualTo(DisplayName))
	{
		return;
	}

	SetFlags(RF_Transactional);
	Modify();

	DisplayName = NewDisplayName;
}

bool UMovieSceneNameableTrack::ValidateDisplayName(const FText& NewDisplayName, FText& OutErrorMessage) const
{
	if (NewDisplayName.IsEmpty())
	{
		OutErrorMessage = LOCTEXT("RenameFailed_LeftBlank", "Labels cannot be left blank");
		return false;
	}
	else if (NewDisplayName.ToString().Len() >= NAME_SIZE)
	{
		OutErrorMessage = FText::Format(LOCTEXT("RenameFailed_TooLong", "Names must be less than {0} characters long"), NAME_SIZE);
		return false;
	}
	return true;
}

#endif


/* UMovieSceneTrack interface
 *****************************************************************************/

#if WITH_EDITORONLY_DATA

FText UMovieSceneNameableTrack::GetDisplayName() const
{
	if (DisplayName.IsEmpty())
	{
		return GetDefaultDisplayName();
	}

	return DisplayName;
}

FText UMovieSceneNameableTrack::GetDefaultDisplayName() const

{ 
	return LOCTEXT("UnnamedTrackName", "Unnamed Track"); 
}

#endif


#undef LOCTEXT_NAMESPACE
