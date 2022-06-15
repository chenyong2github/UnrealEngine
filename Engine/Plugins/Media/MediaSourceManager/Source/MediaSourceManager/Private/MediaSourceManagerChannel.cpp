// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaSourceManagerChannel.h"

#include "MediaAssets/ProxyMediaSource.h"

#define LOCTEXT_NAMESPACE "MediaSourceManagerChannel"

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
