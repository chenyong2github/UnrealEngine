// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "MagicLeapCameraPreviewPlayer.h"
#include "IMagicLeapCameraPlugin.h"
#include "MagicLeapCameraPreviewModule.h"
#include "Misc/Paths.h"
#include "Lumin/LuminPlatformFile.h"
#include "Lumin/LuminPlatformMisc.h"
#include "IMediaEventSink.h"
#include "MediaSamples.h"
#include "Misc/CoreDelegates.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"
#include "ExternalTexture.h"
#include "ExternalTextureRendererGL.h"
#include "MagicLeapHelperVulkan.h"

struct FMagicLeapVideoTextureData
{
public:
	FMagicLeapVideoTextureData()
		: bIsVideoTextureValid(false)
		, PreviousNativeBuffer(ML_INVALID_HANDLE)
	{}

	FTextureRHIRef VideoTexture;
	bool bIsVideoTextureValid;
	MLHandle PreviousNativeBuffer;
};

struct FMagicLeapVideoTextureDataVK : public FMagicLeapVideoTextureData
{
	FSamplerStateRHIRef VideoSampler;
	TMap<uint64, FTextureRHIRef> VideoTexturePool;
};

struct FMagicLeapVideoTextureDataGL : public FMagicLeapVideoTextureData
{
public:

	FMagicLeapVideoTextureDataGL()
	{}

	FExternalTextureRendererGL TextureRenderer;
};

FMagicLeapCameraPreviewPlayer::FMagicLeapCameraPreviewPlayer(IMediaEventSink& InEventSink)
	: bMediaPrepared(false)
	, CurrentState(EMediaState::Closed)
	, EventSink(InEventSink)
	, Samples(MakeShared<FMediaSamples, ESPMode::ThreadSafe>())
	, bWasMediaPlayingBeforeAppPause(false)
	, bPlaybackCompleted(false)
{
	if (FLuminPlatformMisc::ShouldUseVulkan())
	{
		TextureData = MakeShared<FMagicLeapVideoTextureDataVK, ESPMode::ThreadSafe>();
	}
	else
	{
		TextureData = MakeShared<FMagicLeapVideoTextureDataGL, ESPMode::ThreadSafe>();
	}

	CurrentState = Samples.IsValid() ? EMediaState::Closed : EMediaState::Error;
}

FMagicLeapCameraPreviewPlayer::~FMagicLeapCameraPreviewPlayer()
{
	Close();

	MLHandle PreviewHandle = IMagicLeapCameraPlugin::Get().GetPreviewHandle();
	if (MLHandleIsValid(PreviewHandle) && GSupportsImageExternal)
	{
		if (FLuminPlatformMisc::ShouldUseVulkan())
		{
			// Unregister the external texture on render thread
			struct FReleaseVideoResourcesParams
			{
				FGuid PlayerGuid;
			};

			FReleaseVideoResourcesParams Params = { PlayerGuid };

			ENQUEUE_RENDER_COMMAND(MagicLeapMediaPlayerDestroy)(
				[Params](FRHICommandListImmediate& RHICmdList)
				{
					FExternalTextureRegistry::Get().UnregisterExternalTexture(Params.PlayerGuid);
				});
		}
		else
		{
			// Unregister the external texture on render thread
			struct FReleaseVideoResourcesParams
			{
				// Can we make this a TWeakPtr? We need to ensure that FMagicLeapMediaPlayer_::TextureData is not destroyed before
				// we unregister the external texture and releae the VideoTexture.
				TSharedPtr<FMagicLeapVideoTextureDataGL, ESPMode::ThreadSafe> TextureData;
				FGuid PlayerGuid;
			};

			FReleaseVideoResourcesParams Params = { StaticCastSharedPtr<FMagicLeapVideoTextureDataGL>(TextureData), PlayerGuid };

			ENQUEUE_RENDER_COMMAND(MagicLeapMediaPlayerDestroy)(
				[Params](FRHICommandListImmediate& RHICmdList)
				{
					FExternalTextureRegistry::Get().UnregisterExternalTexture(Params.PlayerGuid);
					// @todo: this causes a crash
					//Params.TextureData->VideoTexture->Release();
					Params.TextureData->TextureRenderer.DestroyImageKHR();
				});
		}

		FlushRenderingCommands();
	}
}

void FMagicLeapCameraPreviewPlayer::Close()
{
	if (CurrentState == EMediaState::Closed || CurrentState == EMediaState::Error)
	{
		return;
	}

	{
		FScopeLock Lock(&CriticalSection);
		bPlaybackCompleted = true;
	}

	// remove delegates if registered
	if (ResumeHandle.IsValid())
	{
		FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Remove(ResumeHandle);
		ResumeHandle.Reset();
	}

	if (PauseHandle.IsValid())
	{
		FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(PauseHandle);
		PauseHandle.Reset();
	}

	FMagicLeapCameraDisconnect DummyDisconnectCallback;
	IMagicLeapCameraPlugin::Get().CameraDisconnect(DummyDisconnectCallback);

	CurrentState = EMediaState::Closed;

	bMediaPrepared = false;
	TrackInfo.Empty();
	SelectedTrack.Empty();

	// notify listeners
	EventSink.ReceiveMediaEvent(EMediaEvent::TracksChanged);
	EventSink.ReceiveMediaEvent(EMediaEvent::MediaClosed);
}

IMediaCache& FMagicLeapCameraPreviewPlayer::GetCache()
{
	return *this;
}

IMediaControls& FMagicLeapCameraPreviewPlayer::GetControls()
{
	return *this;
}

FString FMagicLeapCameraPreviewPlayer::GetInfo() const
{
	return FString();
}

FName FMagicLeapCameraPreviewPlayer::GetPlayerName() const
{
	static FName PlayerName(TEXT("MagicLeapCameraPreview"));
	return PlayerName;
}

IMediaSamples& FMagicLeapCameraPreviewPlayer::GetSamples()
{
	return *Samples.Get();
}

FString FMagicLeapCameraPreviewPlayer::GetStats() const
{
	return TEXT("MagicLeapCameraPreview stats information not implemented yet");
}

IMediaTracks& FMagicLeapCameraPreviewPlayer::GetTracks()
{
	return *this;
}

FString FMagicLeapCameraPreviewPlayer::GetUrl() const
{
	return FString();
}

IMediaView& FMagicLeapCameraPreviewPlayer::GetView()
{
	return *this;
}

bool FMagicLeapCameraPreviewPlayer::Open(const FString& Url, const IMediaOptions* Options)
{
	if (CurrentState == EMediaState::Error)
	{
		EventSink.ReceiveMediaEvent(EMediaEvent::MediaOpenFailed);
		return false;
	}

	Close();

	FMagicLeapCameraConnect DummyConnectCallback;
	IMagicLeapCameraPlugin::Get().CameraConnect(DummyConnectCallback);

	EventSink.ReceiveMediaEvent(EMediaEvent::MediaConnecting);

	CurrentState = EMediaState::Preparing;

	return true;
}

bool FMagicLeapCameraPreviewPlayer::Open(const TSharedRef<FArchive, ESPMode::ThreadSafe>& Archive, const FString& OriginalUrl, const IMediaOptions* Options)
{
	return false;
}

bool FMagicLeapCameraPreviewPlayer::CanControl(EMediaControl Control) const
{
	if (Control == EMediaControl::Pause)
	{
		return (CurrentState == EMediaState::Playing);
	}

	if (Control == EMediaControl::Resume)
	{
		return (CurrentState == EMediaState::Paused);
	}

	return false;
}

FTimespan FMagicLeapCameraPreviewPlayer::GetDuration() const
{
	return FTimespan::Zero();
}

float FMagicLeapCameraPreviewPlayer::GetRate() const
{
	return (CurrentState == EMediaState::Playing) ? 1.0f : 0.0f;
}

EMediaState FMagicLeapCameraPreviewPlayer::GetState() const
{
	return CurrentState;
}

EMediaStatus FMagicLeapCameraPreviewPlayer::GetStatus() const
{
	return CurrentState == EMediaState::Preparing ? EMediaStatus::Connecting : EMediaStatus::None;
}

TRangeSet<float> FMagicLeapCameraPreviewPlayer::GetSupportedRates(EMediaRateThinning Thinning) const
{
	TRangeSet<float> Result;

	Result.Add(TRange<float>(0.0f));
	Result.Add(TRange<float>(1.0f));

	return Result;
}

FTimespan FMagicLeapCameraPreviewPlayer::GetTime() const
{
	return FTimespan::Zero();
}

bool FMagicLeapCameraPreviewPlayer::IsLooping() const
{
	return false;
}

bool FMagicLeapCameraPreviewPlayer::Seek(const FTimespan& Time)
{
	return false;
}

bool FMagicLeapCameraPreviewPlayer::SetLooping(bool Looping)
{
	return false;
}

bool FMagicLeapCameraPreviewPlayer::SetRate(float Rate)
{
	if ((CurrentState == EMediaState::Closed) || (CurrentState == EMediaState::Error) || (CurrentState == EMediaState::Preparing))
	{
		UE_LOG(LogMagicLeapCameraPreview, Warning, TEXT("Cannot set rate while closed, preparing, or in error state"));
		return false;
	}

	if (Rate == GetRate())
	{
		// rate already set
		return true;
	}

	bool bResult = false;
	if (Rate == 0.0f)
	{
		CurrentState = EMediaState::Paused;
		EventSink.ReceiveMediaEvent(EMediaEvent::PlaybackSuspended);
	}
	else if (Rate == 1.0f)
	{
		CurrentState = EMediaState::Playing;
		EventSink.ReceiveMediaEvent(EMediaEvent::PlaybackResumed);
		bResult = true;
	}
	else
	{
		UE_LOG(LogMagicLeapCameraPreview, Error, TEXT("Rate %f not supported by MagicLeapMedia."), Rate);
		bResult = false;
	}

	return bResult;
}

bool FMagicLeapCameraPreviewPlayer::SetNativeVolume(float Volume)
{
	return false;
}

void FMagicLeapCameraPreviewPlayer::SetGuid(const FGuid& Guid)
{
	PlayerGuid = Guid;
}

void FMagicLeapCameraPreviewPlayer::TickFetch(FTimespan DeltaTime, FTimespan Timecode)
{
	if (CurrentState != EMediaState::Playing && CurrentState != EMediaState::Paused)
	{
		return;
	}

	if (GSupportsImageExternal)
	{
		MLHandle PreviewHandle = IMagicLeapCameraPlugin::Get().GetPreviewHandle();
		if (!MLHandleIsValid(PreviewHandle))
		{
			return;
		}

		if (FLuminPlatformMisc::ShouldUseVulkan())
		{
			struct FWriteVideoSampleParams
			{
				FMagicLeapCameraPreviewPlayer* MediaPlayer;
				TWeakPtr<FMagicLeapVideoTextureDataVK, ESPMode::ThreadSafe> TextureData;
				FGuid PlayerGuid;
				MLHandle PreviewHandle;
			};

			FWriteVideoSampleParams Params = { this, StaticCastSharedPtr<FMagicLeapVideoTextureDataVK>(TextureData), PlayerGuid, PreviewHandle };

			ENQUEUE_RENDER_COMMAND(MagicLeapMediaPlayerWriteVideoSample)(
				[Params](FRHICommandListImmediate& RHICmdList)
				{
					auto TextureDataPtr = Params.TextureData.Pin();

					if (TextureDataPtr->PreviousNativeBuffer != 0 && MLHandleIsValid(TextureDataPtr->PreviousNativeBuffer))
					{
						TextureDataPtr->PreviousNativeBuffer = 0;
					}

					MLHandle NativeBuffer = Params.PreviewHandle;

					{
						FScopeLock Lock(&Params.MediaPlayer->CriticalSection);
						if (Params.MediaPlayer->bPlaybackCompleted)
						{
							TextureDataPtr->VideoTexturePool.Empty();
							Params.MediaPlayer->bPlaybackCompleted = false;
						}
					}

					if (!TextureDataPtr->VideoTexturePool.Contains((uint64)NativeBuffer))
					{
						FTextureRHIRef NewMediaTexture;
						if (!FMagicLeapHelperVulkan::GetMediaTexture(NewMediaTexture, TextureDataPtr->VideoSampler, NativeBuffer))
						{
							UE_LOG(LogMagicLeapCameraPreview, Error, TEXT("Failed to get next media texture."));
							return;
						}

						TextureDataPtr->VideoTexturePool.Add((uint64)NativeBuffer, NewMediaTexture);

						if (TextureDataPtr->VideoTexture == nullptr)
						{
							FRHIResourceCreateInfo CreateInfo;
							TextureDataPtr->VideoTexture = RHICmdList.CreateTextureExternal2D(1, 1, PF_R8G8B8A8, 1, 1, 0, CreateInfo);
						}

						FMagicLeapHelperVulkan::AliasMediaTexture(TextureDataPtr->VideoTexture, NewMediaTexture);
					}
					else
					{
						FTextureRHIRef* const PooledMediaTexture = TextureDataPtr->VideoTexturePool.Find((uint64)NativeBuffer);
						check(PooledMediaTexture != nullptr);
						FMagicLeapHelperVulkan::AliasMediaTexture(TextureDataPtr->VideoTexture, *PooledMediaTexture);
					}

					if (!TextureDataPtr->bIsVideoTextureValid)
					{
						Params.MediaPlayer->RegisterExternalTexture(Params.PlayerGuid, TextureDataPtr->VideoTexture, TextureDataPtr->VideoSampler);
						TextureDataPtr->bIsVideoTextureValid = true;
					}

					TextureDataPtr->PreviousNativeBuffer = NativeBuffer;
				});
		}
		else
		{
			struct FWriteVideoSampleParams
			{
				FMagicLeapCameraPreviewPlayer* MediaPlayer;
				TWeakPtr<FMagicLeapVideoTextureDataGL, ESPMode::ThreadSafe> TextureData;
				FGuid PlayerGuid;
				MLHandle PreviewHandle;
			};

			FWriteVideoSampleParams Params = { this, StaticCastSharedPtr<FMagicLeapVideoTextureDataGL>(TextureData), PlayerGuid, PreviewHandle };

			ENQUEUE_RENDER_COMMAND(MagicLeapMediaPlayerWriteVideoSample)(
				[Params](FRHICommandListImmediate& RHICmdList)
				{
					auto TextureDataPtr = Params.TextureData.Pin();
					FTextureRHIRef MediaVideoTexture = TextureDataPtr->VideoTexture;
					if (MediaVideoTexture == nullptr)
					{
						FRHIResourceCreateInfo CreateInfo;
						MediaVideoTexture = RHICmdList.CreateTextureExternal2D(1, 1, PF_R8G8B8A8, 1, 1, 0, CreateInfo);
						TextureDataPtr->VideoTexture = MediaVideoTexture;

						if (MediaVideoTexture == nullptr)
						{
							UE_LOG(LogMagicLeapCameraPreview, Warning, TEXT("CreateTextureExternal2D failed!"));
							return;
						}

						TextureDataPtr->bIsVideoTextureValid = false;
					}

					// MLHandle because Unreal's uint64 is 'unsigned long long *' whereas uint64_t for the C-API is 'unsigned long *'
					// TODO: Fix the Unreal types for the above comment.
					MLHandle NativeBuffer = Params.PreviewHandle;
					int32 TextureID = *reinterpret_cast<int32*>(MediaVideoTexture->GetNativeResource());
					TextureDataPtr->TextureRenderer.DestroyImageKHR();
					TextureDataPtr->PreviousNativeBuffer = NativeBuffer;

					if (TextureDataPtr->TextureRenderer.CreateImageKHR(reinterpret_cast<void*>(NativeBuffer)))
					{
						TextureDataPtr->TextureRenderer.BindImageKHRToTexture(TextureID);
						if (!TextureDataPtr->bIsVideoTextureValid)
						{
							FSamplerStateInitializerRHI SamplerStateInitializer(SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp);
							FSamplerStateRHIRef SamplerStateRHI = RHICreateSamplerState(SamplerStateInitializer);
							Params.MediaPlayer->RegisterExternalTexture(Params.PlayerGuid, MediaVideoTexture, SamplerStateRHI);
							TextureDataPtr->bIsVideoTextureValid = true;
						}
					}
				});
		}
	}
}

void FMagicLeapCameraPreviewPlayer::RegisterExternalTexture(const FGuid& InGuid, FTextureRHIRef& InTextureRHI, FSamplerStateRHIRef& InSamplerStateRHI)
{
	FExternalTextureRegistry::Get().RegisterExternalTexture(InGuid, InTextureRHI, InSamplerStateRHI, FLinearColor(1.0f, 0.0f, 0.0f, 1.0f), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
}

void FMagicLeapCameraPreviewPlayer::TickInput(FTimespan DeltaTime, FTimespan Timecode)
{
	if (!bMediaPrepared)
	{
		bMediaPrepared = MLHandleIsValid(IMagicLeapCameraPlugin::Get().GetPreviewHandle());

		if (bMediaPrepared)
		{
			CurrentState = EMediaState::Stopped;

			TrackInfo.Add(EMediaTrackType::Video);
			SelectedTrack.Add(EMediaTrackType::Video, INDEX_NONE);

			TrackInfo[EMediaTrackType::Video].Add(0);
			SelectedTrack[EMediaTrackType::Video] = 0;

			// notify listeners
			EventSink.ReceiveMediaEvent(EMediaEvent::TracksChanged);
			EventSink.ReceiveMediaEvent(EMediaEvent::MediaOpened);
		}
		else
		{
			return;
		}
	}

	if (CurrentState != EMediaState::Playing)
	{
		// remove delegates if registered
		if (ResumeHandle.IsValid())
		{
			FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Remove(ResumeHandle);
			ResumeHandle.Reset();
		}

		if (PauseHandle.IsValid())
		{
			FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(PauseHandle);
			PauseHandle.Reset();
		}

		return;
	}

	// register delegate if not registered
	if (!ResumeHandle.IsValid())
	{
		ResumeHandle = FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddRaw(this, &FMagicLeapCameraPreviewPlayer::HandleApplicationHasEnteredForeground);
	}
	if (!PauseHandle.IsValid())
	{
		PauseHandle = FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddRaw(this, &FMagicLeapCameraPreviewPlayer::HandleApplicationWillEnterBackground);
	}
}

bool FMagicLeapCameraPreviewPlayer::GetAudioTrackFormat(int32 TrackIndex, int32 FormatIndex, FMediaAudioTrackFormat& OutFormat) const
{
	return false;
}

int32 FMagicLeapCameraPreviewPlayer::GetNumTracks(EMediaTrackType TrackType) const
{
	if (TrackInfo.Contains(TrackType))
	{
		return TrackInfo[TrackType].Num();
	}

	return 0;
}

int32 FMagicLeapCameraPreviewPlayer::GetNumTrackFormats(EMediaTrackType TrackType, int32 TrackIndex) const
{
	return ((TrackIndex >= 0) && (TrackIndex < GetNumTracks(TrackType))) ? 1 : 0;
}

int32 FMagicLeapCameraPreviewPlayer::GetSelectedTrack(EMediaTrackType TrackType) const
{
	return TrackType == EMediaTrackType::Video ? 0 : INDEX_NONE;
}

FText FMagicLeapCameraPreviewPlayer::GetTrackDisplayName(EMediaTrackType TrackType, int32 TrackIndex) const
{
	return FText::GetEmpty();
}

int32 FMagicLeapCameraPreviewPlayer::GetTrackFormat(EMediaTrackType TrackType, int32 TrackIndex) const
{
	return (GetSelectedTrack(TrackType) != INDEX_NONE) ? 0 : INDEX_NONE;
}

FString FMagicLeapCameraPreviewPlayer::GetTrackLanguage(EMediaTrackType TrackType, int32 TrackIndex) const
{
	return FString();
}

FString FMagicLeapCameraPreviewPlayer::GetTrackName(EMediaTrackType TrackType, int32 TrackIndex) const
{
	return FString();
}

bool FMagicLeapCameraPreviewPlayer::GetVideoTrackFormat(int32 TrackIndex, int32 FormatIndex, FMediaVideoTrackFormat& OutFormat) const
{
	(void)TrackIndex;
	(void)FormatIndex;
	(void)OutFormat;
	// TODO: Get these dimensions from the native buffer itself.
	OutFormat.Dim = FIntPoint(512, 512);
	// TODO: Don't hardcode. Get from C-API. ml_media_player api does not provide that right now. Try the ml_media_codec api.
	OutFormat.FrameRate = 30.0f;
	OutFormat.FrameRates = TRange<float>(30.0f);
	OutFormat.TypeName = TEXT("BGRA");

	return true;
}

bool FMagicLeapCameraPreviewPlayer::SelectTrack(EMediaTrackType TrackType, int32 TrackIndex)
{
	if (TrackInfo.Contains(TrackType) && CurrentState != EMediaState::Preparing)
	{
		if (TrackInfo[TrackType].IsValidIndex(TrackIndex))
		{
			SelectedTrack[TrackType] = TrackIndex;
			return true;
		}
	}

	return false;
}

bool FMagicLeapCameraPreviewPlayer::SetTrackFormat(EMediaTrackType TrackType, int32 TrackIndex, int32 FormatIndex)
{
	return false;
}

void FMagicLeapCameraPreviewPlayer::HandleApplicationHasEnteredForeground()
{
	// check state in case changed before ticked
	if (CurrentState == EMediaState::Paused && bWasMediaPlayingBeforeAppPause)
	{
		// Unpause
		SetRate(1.0f);
	}
}

void FMagicLeapCameraPreviewPlayer::HandleApplicationWillEnterBackground()
{
	bWasMediaPlayingBeforeAppPause = (CurrentState == EMediaState::Playing);
	// check state in case changed before ticked
	if (bWasMediaPlayingBeforeAppPause)
	{
		// pause
		SetRate(0.0f);
	}
}
