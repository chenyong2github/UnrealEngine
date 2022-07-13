// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaPlateComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "IMediaClock.h"
#include "IMediaClockSink.h"
#include "IMediaModule.h"
#include "MediaComponent.h"
#include "MediaPlateModule.h"
#include "MediaPlayer.h"
#include "MediaPlaylist.h"
#include "MediaSource.h"
#include "MediaSoundComponent.h"
#include "MediaTexture.h"
#include "MediaTextureTracker.h"

#define LOCTEXT_NAMESPACE "MediaPlate"


/**
 * Media clock sink for media textures.
 */
class FMediaComponentClockSink
	: public IMediaClockSink
{
public:

	FMediaComponentClockSink(UMediaPlateComponent* InOwner)
		: Owner(InOwner)
	{ }

	virtual ~FMediaComponentClockSink() { }

	virtual void TickOutput(FTimespan DeltaTime, FTimespan Timecode) override
	{
		if (UMediaPlateComponent* OwnerPtr = Owner.Get())
		{
			Owner->TickOutput();
		}
	}


	/**
	 * Call this when the owner is destroyed.
	 */
	void OwnerDestroyed()
	{
		Owner.Reset();
	}

private:

	TWeakObjectPtr<UMediaPlateComponent> Owner;
};

FLazyName UMediaPlateComponent::MediaComponentName(TEXT("MediaComponent0"));
FLazyName UMediaPlateComponent::MediaPlaylistName(TEXT("MediaPlaylist0"));

UMediaPlateComponent::UMediaPlateComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CacheSettings.bOverride = true;

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
	VisibleMipsTilesCalculations = EMediaTextureVisibleMipsTiles::Plane;
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

void UMediaPlateComponent::BeginDestroy()
{
	if (ClockSink.IsValid())
	{
		// Tell sink we are done.
		ClockSink->OwnerDestroyed();

		IMediaModule* MediaModule = FModuleManager::GetModulePtr<IMediaModule>("Media");
		if (MediaModule != nullptr)
		{
			MediaModule->GetClock().RemoveSink(ClockSink.ToSharedRef());
		}

		ClockSink.Reset();
	}

	Super::BeginDestroy();
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
	else
	{
		// Are we using automatic aspect ratio?
		if ((bIsAspectRatioAuto) &&
			(VisibleMipsTilesCalculations == EMediaTextureVisibleMipsTiles::Plane))
		{
			// Start the clock sink so we can tick.
			IMediaModule* MediaModule = FModuleManager::LoadModulePtr<IMediaModule>("Media");
			if (MediaModule != nullptr)
			{
				if (ClockSink.IsValid() == false)
				{
					ClockSink = MakeShared<FMediaComponentClockSink, ESPMode::ThreadSafe>(this);
				}
				bIsWaitingForRender = true;
				MediaModule->GetClock().AddSink(ClockSink.ToSharedRef());
			}
		}
	}
}

void UMediaPlateComponent::Stop()
{
	TObjectPtr<UMediaPlayer> MediaPlayer = GetMediaPlayer();
	if (MediaPlayer != nullptr)
	{
		MediaPlayer->Close();
	}

	StopClockSink();
}

void UMediaPlateComponent::SetMeshRange(FVector2D InMeshRange)
{
	MeshRange = InMeshRange;

	if (MediaTextureTrackerObject != nullptr)
	{
		MediaTextureTrackerObject->MeshRange = MeshRange;
	}
}

void UMediaPlateComponent::RegisterWithMediaTextureTracker()
{
	// Set up object.
	MediaTextureTrackerObject = MakeShared<FMediaTextureTrackerObject, ESPMode::ThreadSafe>();
	MediaTextureTrackerObject->Object = GetOwner();
	MediaTextureTrackerObject->MipMapLODBias = 0.0f;
	MediaTextureTrackerObject->VisibleMipsTilesCalculations = VisibleMipsTilesCalculations;
	MediaTextureTrackerObject->MeshRange = MeshRange;

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
		// Set cache settings.
		InMediaSource->SetCacheSettings(CacheSettings);
		
		// Set media options.
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

void UMediaPlateComponent::SetAspectRatio(float AspectRatio)
{
	// Get the static mesh.
	if (StaticMeshComponent != nullptr)
	{
		// Update the scale.
		float Height = 1.0f;
		if (AspectRatio != 0.0f)
		{
			Height = 1.0f / AspectRatio;
		}
		FVector Scale(1.0f, 1.0f, Height);
#if WITH_EDITOR
		StaticMeshComponent->Modify();
#endif // WITH_EDITOR
		StaticMeshComponent->SetRelativeScale3D(Scale);
	}
}

void UMediaPlateComponent::TickOutput()
{
	TObjectPtr<UMediaPlayer> MediaPlayer = GetMediaPlayer();
	if (MediaPlayer != nullptr)
	{
		// Is the player ready?
		if (MediaPlayer->IsPreparing() == false)
		{
			if (bIsWaitingForRender)
			{
				bIsWaitingForRender = false;
			}
			else
			{
				// Get the texture.
				UMediaTexture* Texture = GetMediaTexture();
				if (Texture != nullptr)
				{
					float Width = Texture->GetSurfaceWidth();
					float Height = Texture->GetSurfaceHeight();
					// Set aspect ratio.
					if (Height != 0.0f)
					{
						float AspectRatio = Width / Height;
						SetAspectRatio(AspectRatio);
					}

					// No need to tick anymore.
					StopClockSink();
				}
			}
		}
	}
}

void UMediaPlateComponent::StopClockSink()
{
	if (ClockSink.IsValid())
	{
		IMediaModule* MediaModule = FModuleManager::GetModulePtr<IMediaModule>("Media");
		if (MediaModule != nullptr)
		{
			MediaModule->GetClock().RemoveSink(ClockSink.ToSharedRef());
		}
	}
}

#if WITH_EDITOR

void UMediaPlateComponent::OnVisibleMipsTilesCalculationsChange()
{
	if (MediaTextureTrackerObject != nullptr)
	{
		MediaTextureTrackerObject->VisibleMipsTilesCalculations = VisibleMipsTilesCalculations;

		// Propagate the change by restarting the player if it is currently playing.
		TObjectPtr<UMediaPlayer> MediaPlayer = GetMediaPlayer();
		if (MediaPlayer != nullptr)
		{
			if (MediaPlayer->IsPlaying())
			{
				MediaPlayer->Close();
				Play();
			}
		}
	}
}

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
		OnVisibleMipsTilesCalculationsChange();
	}
}

#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
