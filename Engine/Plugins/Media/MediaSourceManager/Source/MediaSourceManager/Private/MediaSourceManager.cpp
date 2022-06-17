// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaSourceManager.h"

#define LOCTEXT_NAMESPACE "MediaSourceManager"

void UMediaSourceManager::Validate()
{
	// Check we have some channels.
	int32 NumChannels = 8;

	while (Channels.Num() < NumChannels)
	{
		UMediaSourceManagerChannel* Channel = NewObject<UMediaSourceManagerChannel>(this);
		Channel->Name = FString::FromInt(Channels.Num());
		Channels.Add(Channel);
	}

	// Validate each channel.
	for (UMediaSourceManagerChannel* Channel : Channels)
	{
		if (Channel != nullptr)
		{
			Channel->Validate();
		}
	}
}


#undef LOCTEXT_NAMESPACE
