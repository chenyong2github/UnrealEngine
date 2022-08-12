// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaSourceManagerChannel.h"

#include "Inputs/MediaSourceManagerInput.h"
#include "MediaAssets/ProxyMediaSource.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"

#define LOCTEXT_NAMESPACE "MediaSourceManagerChannel"

void UMediaSourceManagerChannel::Validate()
{
	// Make sure we have an out texture.
	if (OutTexture == nullptr)
	{
		Modify();
		UMediaTexture* MediaTexture = NewObject<UMediaTexture>(this);
		MediaTexture->NewStyleOutput = true;
		MediaTexture->UpdateResource();
		OutTexture = MediaTexture;
	}
}

UMediaPlayer* UMediaSourceManagerChannel::GetMediaPlayer()
{
	// Create a player if we don't have one.
	if (CurrentMediaPlayer == nullptr)
	{
		CurrentMediaPlayer = NewObject<UMediaPlayer>(this, "MediaPlayer", RF_Transient);
		CurrentMediaPlayer->SetLooping(false);
		CurrentMediaPlayer->PlayOnOpen = false;
		if (OutTexture != nullptr)
		{
			UMediaTexture* MediaTexture = Cast<UMediaTexture>(OutTexture);
			if (MediaTexture != nullptr)
			{
				MediaTexture->SetMediaPlayer(CurrentMediaPlayer);
				MediaTexture->UpdateResource();
			}
		}
	}

	return CurrentMediaPlayer;
}

void UMediaSourceManagerChannel::Play()
{
	if (Input != nullptr)
	{
		UMediaSource* MediaSource = Input->GetMediaSource();
		if (MediaSource != nullptr)
		{
			UMediaPlayer* MediaPlayer = GetMediaPlayer();
			if (MediaPlayer != nullptr)
			{
				FMediaPlayerOptions Options;
				Options.PlayOnOpen = EMediaPlayerOptionBooleanOverride::Enabled;;
				Options.Loop = EMediaPlayerOptionBooleanOverride::Enabled;
				MediaPlayer->OpenSourceWithOptions(MediaSource, Options);
			}
		}
	}
}

void UMediaSourceManagerChannel::PostInitProperties()
{
#if WITH_EDITOR
	// Don't run on CDO
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(this,
			&UMediaSourceManagerChannel::OnObjectPropertyChanged);
	}
#endif // WITH_EDITOR

	Super::PostInitProperties();
}

void UMediaSourceManagerChannel::BeginDestroy()
{
#if WITH_EDITOR
	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
#endif // WITH_EDITOR

	Super::BeginDestroy();
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

void UMediaSourceManagerChannel::OnObjectPropertyChanged(UObject* ObjectBeingModified,
	FPropertyChangedEvent& PropertyChangedEvent)
{
	// Did our media source change?
	if (Input != nullptr)
	{
		UMediaSource* MediaSource = Input->GetMediaSource();
		if (MediaSource != nullptr)
		{
			if (MediaSource == ObjectBeingModified)
			{
				// Restart playback.
				Play();

				// Inform everyone.
				if (OnInputPropertyChanged.IsBound())
				{
					OnInputPropertyChanged.Broadcast();
				}
			}
		}
	}
}

#endif // WITH_EDITOR


#undef LOCTEXT_NAMESPACE
