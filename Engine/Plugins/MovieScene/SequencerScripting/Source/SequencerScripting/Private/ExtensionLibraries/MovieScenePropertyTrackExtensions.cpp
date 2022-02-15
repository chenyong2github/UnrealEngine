// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExtensionLibraries/MovieScenePropertyTrackExtensions.h"
#include "Tracks/MovieSceneByteTrack.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "Tracks/MovieSceneObjectPropertyTrack.h"

void UMovieScenePropertyTrackExtensions::SetPropertyNameAndPath(UMovieScenePropertyTrack* Track, const FName& InPropertyName, const FString& InPropertyPath)
{
	Track->Modify();
	Track->SetPropertyNameAndPath(InPropertyName, InPropertyPath);
}

FName UMovieScenePropertyTrackExtensions::GetPropertyName(UMovieScenePropertyTrack* Track)
{
	return Track->GetPropertyName();
}

FString UMovieScenePropertyTrackExtensions::GetPropertyPath(UMovieScenePropertyTrack* Track)
{
	return Track->GetPropertyPath().ToString();
}

FName UMovieScenePropertyTrackExtensions::GetUniqueTrackName(UMovieScenePropertyTrack* Track)
{
#if WITH_EDITORONLY_DATA
	return Track->UniqueTrackName;
#else
	return NAME_None;
#endif
}

void UMovieScenePropertyTrackExtensions::SetObjectPropertyClass(UMovieSceneObjectPropertyTrack* Track, UClass* PropertyClass)
{
	Track->PropertyClass = PropertyClass;
}

UClass* UMovieScenePropertyTrackExtensions::GetObjectPropertyClass(UMovieSceneObjectPropertyTrack* Track)
{
	return Track->PropertyClass;
}

void UMovieScenePropertyTrackExtensions::SetByteTrackEnum(UMovieSceneByteTrack* Track, UEnum* InEnum)
{
	if (!Track)
	{
		FFrame::KismetExecutionMessage(TEXT("Cannot call SetByteTrackEnum on a null track"), ELogVerbosity::Error);
		return;
	}

	Track->SetEnum(InEnum);
}

UEnum* UMovieScenePropertyTrackExtensions::GetByteTrackEnum(UMovieSceneByteTrack* Track)
{
	if (!Track)
	{
		FFrame::KismetExecutionMessage(TEXT("Cannot call GetByteTrackEnum on a null track"), ELogVerbosity::Error);
		return nullptr;
	}

	return Track->GetEnum();
}


