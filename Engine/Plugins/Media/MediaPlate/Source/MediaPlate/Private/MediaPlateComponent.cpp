// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaPlateComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "MediaComponent.h"
#include "MediaPlateModule.h"
#include "MediaPlayer.h"
#include "MediaPlaylist.h"
#include "MediaSource.h"
#include "MediaSoundComponent.h"
#include "MediaTexture.h"
#include "MediaTextureTracker.h"

#define LOCTEXT_NAMESPACE "MediaPlate"

FLazyName UMediaPlateComponent::MediaComponentName(TEXT("MediaComponent0"));
FLazyName UMediaPlateComponent::MediaPlaylistName(TEXT("MediaPlaylist0"));

UMediaPlateComponent::UMediaPlateComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Set up media component.
	MediaComponent = CreateDefaultSubobject<UMediaComponent>(MediaComponentName);
	if (MediaComponent != nullptr)
	{
		// Set up media texture.
		UMediaTexture* MediaTexture = MediaComponent->GetMediaTexture();
		if (MediaTexture != nullptr)
		{
			MediaTexture->NewStyleOutput = true;
		}
	}

	// Set up playlist.
	MediaPlaylist = CreateDefaultSubobject<UMediaPlaylist>(MediaPlaylistName);

	// Default to plane since AMediaPlate defaults to SM_MediaPlateScreen
	VisibleMipsTilesCalculations = EMediaPlateVisibleMipsTiles::Plane;
}

void UMediaPlateComponent::OnRegister()
{
	Super::OnRegister();

	// Set up sound component if we have one.
	if (SoundComponent != nullptr)
	{
		TObjectPtr<UMediaPlayer> MediaPlayer = GetMediaPlayer();
		if (MediaPlayer != nullptr)
		{
			SoundComponent->SetMediaPlayer(MediaPlayer);
		}
	}
}

void UMediaPlateComponent::BeginPlay()
{
	Super::BeginPlay();

	// Start playing?
	if (bAutoPlay)
	{
		Play();
	}
}

UMediaPlayer* UMediaPlateComponent::GetMediaPlayer()
{
	TObjectPtr<UMediaPlayer> MediaPlayer = nullptr;
	if (MediaComponent != nullptr)
	{
		MediaPlayer = MediaComponent->GetMediaPlayer();
	}

	return MediaPlayer;
}

UMediaTexture* UMediaPlateComponent::GetMediaTexture()
{
	TObjectPtr<UMediaTexture> MediaTexture = nullptr;
	if (MediaComponent != nullptr)
	{
		MediaTexture = MediaComponent->GetMediaTexture();
	}

	return MediaTexture;
}

void UMediaPlateComponent::Play()
{
	bool bIsPlaying = false;
	TObjectPtr<UMediaPlayer> MediaPlayer = GetMediaPlayer();
	if (MediaPlayer != nullptr)
	{
		UMediaSource* MediaSource = nullptr;
		if (MediaPlaylist != nullptr)
		{
			MediaSource = MediaPlaylist->Get(0);
		}
		bIsPlaying = PlayMediaSource(MediaSource);
	}

	// Did anything play?
	if (bIsPlaying == false)
	{
		UE_LOG(LogMediaPlate, Warning, TEXT("Could not play anything."));
	}
}

void UMediaPlateComponent::Stop()
{
	TObjectPtr<UMediaPlayer> MediaPlayer = GetMediaPlayer();
	if (MediaPlayer != nullptr)
	{
		MediaPlayer->Close();
	}
}

void UMediaPlateComponent::RegisterWithMediaTextureTracker()
{
	// Set up object.
	MediaTextureTrackerObject = MakeShared<FMediaTextureTrackerObject, ESPMode::ThreadSafe>();
	MediaTextureTrackerObject->Object = GetOwner();
	MediaTextureTrackerObject->MipMapLODBias = 0.0f;
	MediaTextureTrackerObject->VisibleMipsTilesCalculations = static_cast<EMediaTextureVisibleMipsTiles>(VisibleMipsTilesCalculations);

	// Add our texture.
	TObjectPtr<UMediaTexture> MediaTexture = GetMediaTexture();
	if (MediaTexture != nullptr)
	{
		FMediaTextureTracker& MediaTextureTracker = FMediaTextureTracker::Get();
		MediaTextureTracker.RegisterTexture(MediaTextureTrackerObject, MediaTexture);
	}
}

void UMediaPlateComponent::UnregisterWithMediaTextureTracker()
{
	// Remove out texture.
	if (MediaTextureTrackerObject != nullptr)
	{
		FMediaTextureTracker& MediaTextureTracker = FMediaTextureTracker::Get();
		TObjectPtr<UMediaTexture> MediaTexture = GetMediaTexture();
		MediaTextureTracker.UnregisterTexture(MediaTextureTrackerObject, MediaTexture);
	}
}

bool UMediaPlateComponent::PlayMediaSource(UMediaSource* InMediaSource)
{
	bool bIsPlaying = false;

	if (InMediaSource != nullptr)
	{
		// Set media options.
		InMediaSource->SetMediaOptionBool(TEXT("ImgMediaSmartCacheEnabled"), bSmartCacheEnabled);
		InMediaSource->SetMediaOptionFloat(TEXT("ImgMediaSmartCacheTimeToLookAhead"), SmartCacheTimeToLookAhead);
	
		TObjectPtr<UMediaPlayer> MediaPlayer = GetMediaPlayer();
		if (MediaPlayer != nullptr)
		{
			// Play the source.
			FMediaPlayerOptions Options;
			Options.SeekTime = FTimespan::FromSeconds(StartTime);
			Options.PlayOnOpen = EMediaPlayerOptionBooleanOverride::Enabled;
			Options.Loop = bLoop ? EMediaPlayerOptionBooleanOverride::Enabled :
				EMediaPlayerOptionBooleanOverride::Disabled;
			bIsPlaying = MediaPlayer->OpenSourceWithOptions(InMediaSource, Options);
		}
	}

	return bIsPlaying;
}

#if WITH_EDITOR

void UMediaPlateComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Has bEnableAudiio changed?
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(ThisClass, bEnableAudio))
	{
		// Are we turning on audio?
		if (bEnableAudio)
		{
			// Get the media player.
			TObjectPtr<UMediaPlayer> MediaPlayer = GetMediaPlayer();
			if (MediaPlayer != nullptr)
			{
				// Create a sound component.
				SoundComponent = NewObject<UMediaSoundComponent>(this, NAME_None);
				if (SoundComponent != nullptr)
				{
					SoundComponent->bIsUISound = true;
					SoundComponent->SetMediaPlayer(MediaPlayer);
					SoundComponent->Initialize();
					SoundComponent->RegisterComponent();
				}
			}
		}
		else
		{
			// Remove this sound component.
			if (SoundComponent != nullptr)
			{
				SoundComponent->UnregisterComponent();
				SoundComponent->SetMediaPlayer(nullptr);
				SoundComponent->UpdatePlayer();
				SoundComponent->DestroyComponent();
				SoundComponent = nullptr;
			}
		}
	}
	else if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(ThisClass, VisibleMipsTilesCalculations))
	{
		if (MediaTextureTrackerObject != nullptr)
		{
			MediaTextureTrackerObject->VisibleMipsTilesCalculations = static_cast<EMediaTextureVisibleMipsTiles>(VisibleMipsTilesCalculations);
		}
	}
}

#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
