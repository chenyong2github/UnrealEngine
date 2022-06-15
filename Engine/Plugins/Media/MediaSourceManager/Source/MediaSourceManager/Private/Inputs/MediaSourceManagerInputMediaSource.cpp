// Copyright Epic Games, Inc. All Rights Reserved.

#include "Inputs/MediaSourceManagerInputMediaSource.h"

#include "MediaSource.h"

#define LOCTEXT_NAMESPACE "MediaSourceManagerInputMediaSource"

FString UMediaSourceManagerInputMediaSource::GetDisplayName()
{
	FString Name = FString(TEXT("<none>"));

	if (MediaSource != nullptr)
	{
		Name = MediaSource->GetName();
	}

	return Name;
}

UMediaSource* UMediaSourceManagerInputMediaSource::GetMediaSource()
{
	return MediaSource;
}

#undef LOCTEXT_NAMESPACE
