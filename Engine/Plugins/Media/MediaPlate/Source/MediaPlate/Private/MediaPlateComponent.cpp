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
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	CacheSettings.bOverride = true;

	// Set up playlist.
	MediaPlaylist = CreateDefaultSubobject<UMediaPlaylist>(MediaPlaylistName);

	// Default to plane since AMediaPlate defaults to SM_MediaPlateScreen
	VisibleMipsTilesCalculations = EMediaTextureVisibleMipsTiles::Plane;
}

void UMediaPlateComponent::OnRegister()
{
	Super::OnRegister();

	// Create media texture if we don't have one.
	if (MediaTexture == nullptr)
	{
		MediaTexture = NewObject<UMediaTexture>(this);
		MediaTexture->NewStyleOutput = true;
	}

	// Create media player if we don't have one.
	if (MediaPlayer == nullptr)
	{
		MediaPlayer = NewObject<UMediaPlayer>(this);
		MediaPlayer->SetLooping(false);
		MediaPlayer->PlayOnOpen = false;
	}

	// Set up media texture.
	if (MediaTexture != nullptr)
	{
		MediaTexture->SetMediaPlayer(MediaPlayer);
		MediaTexture->UpdateResource();
	}

	// Set up sound component if we have one.
	if (SoundComponent != nullptr)
	{
		if (MediaPlayer != nullptr)
		{
			SoundComponent->SetMediaPlayer(MediaPlayer);
		}
	}
}

void UMediaPlateComponent::BeginPlay()
{
	Super::BeginPlay();

	// Activate tick if needed.
	UpdateTicking();

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


void UMediaPlateComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bPlayOnlyWhenVisible)
	{
		bool bIsVisible = IsVisible();

		if (MediaPlayer != nullptr)
		{
			if (bIsVisible)
			{
				ResumeWhenVisible();
			}
			else
			{
				if (MediaPlayer->IsPlaying())
				{
					MediaPlayer->Pause();
					TimeWhenPlaybackPaused = FApp::GetGameTime();
				}
			}
		}
	}
}

UMediaPlayer* UMediaPlateComponent::GetMediaPlayer()
{
	return MediaPlayer;
}

UMediaTexture* UMediaPlateComponent::GetMediaTexture()
{
	return MediaTexture;
}

void UMediaPlateComponent::Play()
{
	if ((bPlayOnlyWhenVisible == false) || (IsVisible()))
	{
		bool bIsPlaying = false;
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
					MediaModule->GetClock().AddSink(ClockSink.ToSharedRef());
				}
			}
		}
	}
	else
	{
		bWantsToPlayWhenVisible = true;
		TimeWhenPlaybackPaused = FApp::GetGameTime();
	}
}

void UMediaPlateComponent::Stop()
{
	if (MediaPlayer != nullptr)
	{
		MediaPlayer->Close();
	}

	StopClockSink();
	bWantsToPlayWhenVisible = false;
}

void UMediaPlateComponent::SetMeshRange(FVector2D InMeshRange)
{
	MeshRange = InMeshRange;

	if (MediaTextureTrackerObject != nullptr)
	{
		MediaTextureTrackerObject->MeshRange = MeshRange;
	}
}

void UMediaPlateComponent::SetPlayOnlyWhenVisible(bool bInPlayOnlyWhenVisible)
{
	// If we are turning off PlayOnlyWhenVisible then make sure we are playing.
	if (bInPlayOnlyWhenVisible == false)
	{
		ResumeWhenVisible();
	}

	bPlayOnlyWhenVisible = bInPlayOnlyWhenVisible;
	UpdateTicking();
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
	if (MediaPlayer != nullptr)
	{
		// Is the player ready?
		if (MediaPlayer->IsPreparing() == false)
		{
			FIntPoint VideoDim = MediaPlayer->GetVideoTrackDimensions(INDEX_NONE, INDEX_NONE);
			if (VideoDim.Y != 0)
			{ 
				// Set aspect ratio.
				float AspectRatio = (float)VideoDim.X / (float)VideoDim.Y;
				SetAspectRatio(AspectRatio);
					
				// No need to tick anymore.
				StopClockSink();
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

bool UMediaPlateComponent::IsVisible()
{
#if WITH_EDITOR
	// Always return true if the game is not running as the visibility checks do not work in this
	// case.
	if (FApp::IsGame() == false)
	{
		return true;
	}
#endif

	return GetOwner()->WasRecentlyRendered();
}

void UMediaPlateComponent::ResumeWhenVisible()
{
	if (MediaPlayer != nullptr)
	{
		if (MediaPlayer->IsPaused())
		{
			FTimespan PlayTime = GetResumeTime();
			MediaPlayer->Seek(PlayTime);
			MediaPlayer->Play();
		}
		else if (bWantsToPlayWhenVisible)
		{
			Play();
			FTimespan PlayTime = GetResumeTime();
			MediaPlayer->Seek(PlayTime);
		}
	}
}

FTimespan UMediaPlateComponent::GetResumeTime()
{
	FTimespan PlayerTime;
	if (MediaPlayer != nullptr)
	{
		PlayerTime = MediaPlayer->GetTime();
		float CurrentTime = FApp::GetGameTime();
		float ElapsedTime = CurrentTime - TimeWhenPlaybackPaused;
		PlayerTime += FTimespan::FromSeconds(ElapsedTime);
	}

	return PlayerTime;
}

void UMediaPlateComponent::UpdateTicking()
{
	bool bEnableTick = bPlayOnlyWhenVisible;
	PrimaryComponentTick.SetTickFunctionEnable(bEnableTick);
}

#if WITH_EDITOR

void UMediaPlateComponent::OnVisibleMipsTilesCalculationsChange()
{
	if (MediaTextureTrackerObject != nullptr)
	{
		MediaTextureTrackerObject->VisibleMipsTilesCalculations = VisibleMipsTilesCalculations;

		// Propagate the change by restarting the player if it is currently playing.
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
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Has bEnableAudiio changed?
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(ThisClass, bEnableAudio))
	{
		// Are we turning on audio?
		if (bEnableAudio)
		{
			// Get the media player.
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
