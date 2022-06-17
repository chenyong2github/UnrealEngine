// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaSourceManagerChannel.h"

#include "MediaAssets/ProxyMediaSource.h"
#include "MediaTexture.h"

#define LOCTEXT_NAMESPACE "MediaSourceManagerChannel"

void UMediaSourceManagerChannel::Validate()
{
	// Make sure we have an out texture.
	if (OutTexture == nullptr)
	{
		Modify();
		OutTexture = NewObject<UMediaTexture>(this);
		OutTexture->UpdateResource();
	}
}

#if WITH_EDITOR

void UMediaSourceManagerChannel::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Did the input media source change?
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(ThisClass, InMediaSource))
	{
		// Hook up the proxy media source to this.
		if (OutMediaSource != nullptr)
		{
			OutMediaSource->SetMediaSource(InMediaSource);
			OutMediaSource->MarkPackageDirty();
		}
	}
}

#endif // WITH_EDITOR


#undef LOCTEXT_NAMESPACE
