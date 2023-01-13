// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneMediaData.h"

#include "IMediaEventSink.h"
#include "MediaPlayer.h"
#include "MediaPlayerProxyInterface.h"
#include "MediaTexture.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"


/* FMediaSectionData structors
 *****************************************************************************/

FMovieSceneMediaData::FMovieSceneMediaData()
	: bOverrideMediaPlayer(false)
	, MediaPlayer(nullptr)
	, SeekOnOpenTime(FTimespan::MinValue())
{ }


FMovieSceneMediaData::~FMovieSceneMediaData()
{
	if (MediaPlayer != nullptr)
	{
		MediaPlayer->OnMediaEvent().RemoveAll(this);
		MediaPlayer->Close();
		MediaPlayer->RemoveFromRoot();
	}
}


/* FMediaSectionData interface
 *****************************************************************************/

UMediaPlayer* FMovieSceneMediaData::GetMediaPlayer()
{
	return MediaPlayer;
}


void FMovieSceneMediaData::SeekOnOpen(FTimespan Time)
{
	SeekOnOpenTime = Time;
}


void FMovieSceneMediaData::Setup(UMediaPlayer* OverrideMediaPlayer, UObject* InPlayerProxy, int32 ProxyTextureIndex)
{
	// Ensure we don't already have a media player set. Setup should only be called once
	check(!MediaPlayer);

	if (OverrideMediaPlayer)
	{
		MediaPlayer = OverrideMediaPlayer;
		bOverrideMediaPlayer = true;
	}
	else if (MediaPlayer == nullptr)
	{
		MediaPlayer = NewObject<UMediaPlayer>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UMediaPlayer::StaticClass()));
	}

	MediaPlayer->PlayOnOpen = false;
	MediaPlayer->OnMediaEvent().AddRaw(this, &FMovieSceneMediaData::HandleMediaPlayerEvent);
	MediaPlayer->AddToRoot();
	ProxyMediaTexture.Reset();

	// Do we have a valid proxy object?
	if ((InPlayerProxy != nullptr) && (InPlayerProxy->Implements<UMediaPlayerProxyInterface>()))
	{
		PlayerProxy = InPlayerProxy;
		IMediaPlayerProxyInterface* PlayerProxyInterface = Cast<IMediaPlayerProxyInterface>(PlayerProxy);
		if (PlayerProxyInterface != nullptr)
		{
			ProxyMediaTexture = PlayerProxyInterface->ProxyGetMediaTexture(ProxyTextureIndex);
		}
	}
	else
	{
		PlayerProxy.Reset();
	}
}

void FMovieSceneMediaData::Initialize(bool bIsEvaluating)
{
	if (bIsEvaluating)
	{
		StartUsingProxyMediaTexture();
	}
	else
	{
		StopUsingProxyMediaTexture();
	}
}

void FMovieSceneMediaData::TearDown()
{
	StopUsingProxyMediaTexture();
}

void FMovieSceneMediaData::StartUsingProxyMediaTexture()
{
	if (PlayerProxy != nullptr)
	{
		if (ProxyMediaTexture != nullptr)
		{
			ProxyMediaTexture->SetMediaPlayer(MediaPlayer);
		}
	}
}

void FMovieSceneMediaData::StopUsingProxyMediaTexture()
{
	if (PlayerProxy != nullptr)
	{
		if (ProxyMediaTexture != nullptr)
		{
			if (ProxyMediaTexture->GetMediaPlayer() == MediaPlayer)
			{
				ProxyMediaTexture->SetMediaPlayer(nullptr);
			}
		}
	}
}

/* FMediaSectionData callbacks
 *****************************************************************************/

void FMovieSceneMediaData::HandleMediaPlayerEvent(EMediaEvent Event)
{
	if ((Event != EMediaEvent::MediaOpened) || (SeekOnOpenTime < FTimespan::Zero()))
	{
		return; // we only care about seek on open
	}

	if (!MediaPlayer->SupportsSeeking())
	{
		return; // media can't seek
	}

	const FTimespan Duration = MediaPlayer->GetDuration();
    if (Duration == FTimespan::Zero())
    {
	    return;
    }

	FTimespan MediaTime;
	if (!MediaPlayer->IsLooping())
	{
		MediaTime = FMath::Clamp(SeekOnOpenTime, FTimespan::Zero(), Duration - FTimespan::FromSeconds(0.001));
	}
	else
	{
		MediaTime = SeekOnOpenTime % Duration;
	}

	MediaPlayer->SetRate(0.0f);
	MediaPlayer->Seek(MediaTime);

	SeekOnOpenTime = FTimespan::MinValue();
}
