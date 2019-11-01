// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MagicLeapMediaPlayer.h"
#include "IMagicLeapMediaModule.h"
#include "Misc/Paths.h"
#include "Lumin/LuminPlatformFile.h"
#include "Lumin/CAPIShims/LuminAPILogging.h"
#include "Lumin/CAPIShims/LuminAPIMediaError.h"
#include "Lumin/CAPIShims/LuminAPIMediaFormat.h"
#include "Lumin/CAPIShims/LuminAPISharedFile.h"
#include "Lumin/CAPIShims/LuminAPIFileInfo.h"
#include "IMediaEventSink.h"
#include "IMediaOptions.h"
#include "MagicLeapMediaOverlaySample.h"
#include "MediaSamples.h"
#include "Misc/CoreDelegates.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"
#include "ExternalTexture.h"
#include "ExternalTextureRendererGL.h"
#include "MagicLeapHelperVulkan.h"
#include "HAL/Platform.h"

#define LOCTEXT_NAMESPACE "FMagicLeapMediaModule"

static void *STDCALL _ThreadProc(void *PlayerHandle)
{
	FPlatformMisc::LowLevelOutputDebugString(TEXT("In the detatched thread to destroy media player"));

	MLHandle MediaPlayerHandle = reinterpret_cast<MLHandle>(PlayerHandle);

	MLResult Result = MLMediaPlayerReset(MediaPlayerHandle);
	if (Result != MLResult_Ok)
	{
		FPlatformMisc::LowLevelOutputDebugStringf(TEXT("MLMediaPlayerReset failed with error %s"), MLMediaResultGetString(Result));
	}

	Result = MLMediaPlayerDestroy(MediaPlayerHandle);
	if (Result != MLResult_Ok)
	{
		FPlatformMisc::LowLevelOutputDebugStringf(TEXT("MLMediaPlayerDestroy failed with error %s"), MLMediaResultGetString(Result));
	}

	pthread_exit(nullptr);
	return nullptr;
}

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

FMagicLeapMediaPlayer::FMagicLeapMediaPlayer(IMediaEventSink& InEventSink)
: MediaPlayerHandle(ML_INVALID_HANDLE)
, CaptionParserHandle(ML_INVALID_HANDLE)
, bMediaPrepared(false)
, CurrentState(EMediaState::Closed)
, EventSink(InEventSink)
, Samples(MakeShared<FMediaSamples, ESPMode::ThreadSafe>())
, bWasMediaPlayingBeforeAppPause(false)
, bPlaybackCompleted(false)
, CurrentPlaybackTime(FTimespan::Zero())
{
	if (FLuminPlatformMisc::ShouldUseVulkan())
	{
		TextureData = MakeShared<FMagicLeapVideoTextureDataVK, ESPMode::ThreadSafe>();
	}
	else
	{
		TextureData = MakeShared<FMagicLeapVideoTextureDataGL, ESPMode::ThreadSafe>();
	}	
}

FMagicLeapMediaPlayer::~FMagicLeapMediaPlayer()
{
	Close();
}

void FMagicLeapMediaPlayer::Close()
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

	MLResult StopResult = MLMediaPlayerStop(MediaPlayerHandle);
	UE_CLOG(StopResult != MLResult_Ok, LogMagicLeapMedia, Error, TEXT("MLMediaPlayerStop failed with error %s"), UTF8_TO_TCHAR(MLMediaResultGetString(StopResult)));

	// Moved here from the constructor temporarily, until the freeze in MLMediaPlayerReset()/Destroy() is fixed.
	if (GSupportsImageExternal)
	{
		if (FLuminPlatformMisc::ShouldUseVulkan())
		{
			// Unregister the external texture on render thread
			struct FReleaseVideoResourcesParams
			{
				FMagicLeapMediaPlayer* MediaPlayer;
				TSharedPtr<FMagicLeapVideoTextureDataVK, ESPMode::ThreadSafe> TextureData;
				FGuid PlayerGuid;
				MLHandle MediaPlayerHandle;
			};

			FReleaseVideoResourcesParams Params = { this, StaticCastSharedPtr<FMagicLeapVideoTextureDataVK>(TextureData), PlayerGuid, MediaPlayerHandle };

			ENQUEUE_RENDER_COMMAND(MagicLeapMediaPlayerDestroy)(
				[Params](FRHICommandListImmediate& RHICmdList)
				{
					FExternalTextureRegistry::Get().UnregisterExternalTexture(Params.PlayerGuid);

					if (Params.TextureData->PreviousNativeBuffer != 0 && MLHandleIsValid(Params.TextureData->PreviousNativeBuffer))
					{
						Params.MediaPlayer->RenderThreadReleaseNativeBuffer(Params.MediaPlayerHandle, Params.TextureData->PreviousNativeBuffer);
					}

					Params.TextureData->VideoTexturePool.Empty();

					Params.MediaPlayer->TriggerResetAndDestroy();
				});
		}
		else
		{
			// Unregister the external texture on render thread
			struct FReleaseVideoResourcesParams
			{
				FMagicLeapMediaPlayer* MediaPlayer;
				// Can we make this a TWeakPtr? We need to ensure that FMagicLeapMediaPlayer::TextureData is not destroyed before
				// we unregister the external texture and releae the VideoTexture.
				TSharedPtr<FMagicLeapVideoTextureDataGL, ESPMode::ThreadSafe> TextureData;
				FGuid PlayerGuid;
				MLHandle MediaPlayerHandle;
			};

			FReleaseVideoResourcesParams Params = { this, StaticCastSharedPtr<FMagicLeapVideoTextureDataGL>(TextureData), PlayerGuid, MediaPlayerHandle };

			ENQUEUE_RENDER_COMMAND(MagicLeapMediaPlayerDestroy)(
				[Params](FRHICommandListImmediate& RHICmdList)
				{
					FExternalTextureRegistry::Get().UnregisterExternalTexture(Params.PlayerGuid);
					// @todo: this causes a crash
					//Params.TextureData->VideoTexture->Release();

					Params.TextureData->TextureRenderer.DestroyImageKHR();

					if (Params.TextureData->PreviousNativeBuffer != 0 && MLHandleIsValid(Params.TextureData->PreviousNativeBuffer))
					{
						Params.MediaPlayer->RenderThreadReleaseNativeBuffer(Params.MediaPlayerHandle, Params.TextureData->PreviousNativeBuffer);
					}

					Params.MediaPlayer->TriggerResetAndDestroy();
				});
		}

		FlushRenderingCommands();
	}
	else
	{
		TriggerResetAndDestroy();
	}

	if (MLHandleIsValid(CaptionParserHandle))
	{
		MLResult Result = MLMediaCCParserDestroy(CaptionParserHandle);
		UE_CLOG(Result != MLResult_Ok, LogMagicLeapMedia, Error, TEXT("MLMediaCCParserDestroy failed with error %s."), UTF8_TO_TCHAR(MLGetResultString(Result)));
		CaptionParserHandle = ML_INVALID_HANDLE;
	}

	MediaPlayerHandle = ML_INVALID_HANDLE;
	CurrentState = EMediaState::Closed;

	bMediaPrepared = false;
	Info.Empty();
	MediaUrl = FString();
	TrackInfo.Empty();
	SelectedTrack.Empty();

	// notify listeners
	EventSink.ReceiveMediaEvent(EMediaEvent::TracksChanged);
	EventSink.ReceiveMediaEvent(EMediaEvent::MediaClosed);
}

IMediaCache& FMagicLeapMediaPlayer::GetCache()
{
	return *this;
}

IMediaControls& FMagicLeapMediaPlayer::GetControls()
{
	return *this;
}

FString FMagicLeapMediaPlayer::GetInfo() const
{
	return Info;
}

FName FMagicLeapMediaPlayer::GetPlayerName() const
{
	static FName PlayerName(TEXT("MagicLeapMedia"));
	return PlayerName;
}

IMediaSamples& FMagicLeapMediaPlayer::GetSamples()
{
	return *Samples.Get();
}

FString FMagicLeapMediaPlayer::GetStats() const
{
	return TEXT("MagicLeapMedia stats information not implemented yet");
}

IMediaTracks& FMagicLeapMediaPlayer::GetTracks()
{
	return *this;
}

FString FMagicLeapMediaPlayer::GetUrl() const
{
	return MediaUrl;
}

IMediaView& FMagicLeapMediaPlayer::GetView()
{
	return *this;
}

bool FMagicLeapMediaPlayer::Open(const FString& Url, const IMediaOptions* Options)
{
	// Moved here from the constructor temporarily, until the freeze in MLMediaPlayerReset()/Destroy() is fixed.
	MLResult Result = MLMediaPlayerCreate(&MediaPlayerHandle);
	UE_CLOG(Result != MLResult_Ok, LogMagicLeapMedia, Error, TEXT("MLMediaPlayerCreate failed with error %s."), UTF8_TO_TCHAR(MLMediaResultGetString(Result)));
	CurrentState = (Samples.IsValid() && Result == MLResult_Ok) ? EMediaState::Closed : EMediaState::Error;

	if (CurrentState == EMediaState::Error)
	{
		EventSink.ReceiveMediaEvent(EMediaEvent::MediaOpenFailed);
		return false;
	}

	Close();

	if ((Url.IsEmpty()))
	{
		EventSink.ReceiveMediaEvent(EMediaEvent::MediaOpenFailed);
		return false;
	}

	MediaUrl = Url;

	const FString localFileSchema = "file://";
	const FString sharedFileSchema = "mlshared://";

	// open the media
	if (Url.StartsWith(localFileSchema))
	{
		FString FilePath = Url.RightChop(localFileSchema.Len());
		FPaths::NormalizeFilename(FilePath);

		IPlatformFile& PlatformFile = IPlatformFile::GetPlatformPhysical();
		// This module is only for Lumin so this is fine for now.
		FLuminPlatformFile* LuminPlatformFile = static_cast<FLuminPlatformFile*>(&PlatformFile);
		// make sure the file exists
		if (!LuminPlatformFile->FileExists(*FilePath, FilePath))
		{
			UE_LOG(LogMagicLeapMedia, Error, TEXT("File doesn't exist %s."), *FilePath);
			EventSink.ReceiveMediaEvent(EMediaEvent::MediaOpenFailed);
			return false;
		}

		Result = MLMediaPlayerSetDataSourceForPath(MediaPlayerHandle, TCHAR_TO_UTF8(*FilePath));
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogMagicLeapMedia, Error, TEXT("MLMediaPlayerSetDataSourceForPath for path %s failed with error %s."), *FilePath, UTF8_TO_TCHAR(MLMediaResultGetString(Result)));
			EventSink.ReceiveMediaEvent(EMediaEvent::MediaOpenFailed);
			return false;
		}
	}
	else if (Url.StartsWith(sharedFileSchema))
	{
		const FString SharedFileName = Url.RightChop(sharedFileSchema.Len());
		const char* Filename_UTF8 = TCHAR_TO_UTF8(*SharedFileName);
		MLSharedFileList* SharedFileList = nullptr;
		Result = MLSharedFileRead(&Filename_UTF8, 1, &SharedFileList);
		UE_CLOG(Result != MLResult_Ok, LogMagicLeapMedia, Error, TEXT("MLSharedFileRead(%s) failed with error %s"), *SharedFileName, UTF8_TO_TCHAR(MLSharedFileGetResultString(Result)));

		if (Result == MLResult_Ok && SharedFileList != nullptr)
		{
			MLHandle ListLength = 0;
			Result = MLSharedFileGetListLength(SharedFileList, &ListLength);
			UE_CLOG(Result != MLResult_Ok, LogMagicLeapMedia, Error, TEXT("MLSharedFileGetListLength failed with error %s"), UTF8_TO_TCHAR(MLSharedFileGetResultString(Result)));
			if (Result == MLResult_Ok && ListLength > 0)
			{
				MLFileInfo* FileInfo = nullptr;
				Result = MLSharedFileGetMLFileInfoByIndex(SharedFileList, 0, &FileInfo);
				UE_CLOG(Result != MLResult_Ok, LogMagicLeapMedia, Error, TEXT("MLSharedFileGetMLFileInfoByIndex failed with error %s"), UTF8_TO_TCHAR(MLSharedFileGetResultString(Result)));
				if (Result == MLResult_Ok && FileInfo != nullptr)
				{
					MLFileDescriptor FileHandle = -1;
					Result = MLFileInfoGetFD(FileInfo, &FileHandle);
					UE_CLOG(Result != MLResult_Ok, LogMagicLeapMedia, Error, TEXT("MLFileInfoGetFD failed with error %s"), UTF8_TO_TCHAR(MLGetResultString(Result)));
					if (Result == MLResult_Ok && FileHandle > -1)
					{
						Result = MLMediaPlayerSetDataSourceForFD(MediaPlayerHandle, FileHandle);
						UE_CLOG(Result != MLResult_Ok, LogMagicLeapMedia, Error, TEXT("MLMediaPlayerSetDataSourceForFD failed with error %s"), UTF8_TO_TCHAR(MLMediaResultGetString(Result)));
					}
				}
			}

			MLSharedFileListRelease(&SharedFileList);
		}

		if (Result != MLResult_Ok)
		{
			UE_LOG(LogMagicLeapMedia, Error, TEXT("MLMediaPlayerSetDataSourceForFD for shared file %s failed."), *Url);
			EventSink.ReceiveMediaEvent(EMediaEvent::MediaOpenFailed);
			return false;
		}
	}
	else
	{    
		// open remote media
		Result = MLMediaPlayerSetDataSourceForURI(MediaPlayerHandle, TCHAR_TO_UTF8(*Url));
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogMagicLeapMedia, Error, TEXT("MLMediaPlayerSetDataSourceForURI for remote media source %s failed with error %s."), *Url, UTF8_TO_TCHAR(MLMediaResultGetString(Result)));
			EventSink.ReceiveMediaEvent(EMediaEvent::MediaOpenFailed);
			return false;
		}
	}

	const bool Precache = (Options != nullptr) ? Options->GetMediaOption("PrecacheFile", false) : false;
	UE_CLOG(Precache, LogMagicLeapMedia, Warning, TEXT("Precaching is not supported in Magic Leap Media. Use Magic Leap Media Codec instead if this feature is necessarry."));

	EventSink.ReceiveMediaEvent(EMediaEvent::MediaConnecting);

	// prepare media
	MediaUrl = Url;

	Result = MLMediaPlayerPrepareAsync(MediaPlayerHandle);
	if (Result != MLResult_Ok)
	{
		UE_LOG(LogMagicLeapMedia, Error, TEXT("MLMediaPlayerPrepareAsync for media source %s failed with error %s"), *Url, UTF8_TO_TCHAR(MLMediaResultGetString(Result)));
		EventSink.ReceiveMediaEvent(EMediaEvent::MediaOpenFailed);
		return false;
	}

	CurrentState = EMediaState::Preparing;

	return true;
}

bool FMagicLeapMediaPlayer::Open(const TSharedRef<FArchive, ESPMode::ThreadSafe>& Archive, const FString& OriginalUrl, const IMediaOptions* Options)
{
	// TODO: MagicLeapMedia: implement opening media from FArchive
	return false;
}

bool FMagicLeapMediaPlayer::CanControl(EMediaControl Control) const
{
	if (Control == EMediaControl::Pause)
	{
		return (CurrentState == EMediaState::Playing);
	}

	if (Control == EMediaControl::Resume)
	{
		return (CurrentState == EMediaState::Paused);
	}

	if (Control == EMediaControl::Seek)
	{
		return (CurrentState == EMediaState::Playing) || (CurrentState == EMediaState::Paused);
	}

	return false;
}

FTimespan FMagicLeapMediaPlayer::GetDuration() const
{
	FTimespan Duration = FTimespan::Zero();

	if (CurrentState == EMediaState::Playing || CurrentState == EMediaState::Paused || CurrentState == EMediaState::Stopped)
	{
		int32 durationMilSec = 0;
		MLResult Result = MLMediaPlayerGetDuration(MediaPlayerHandle, &durationMilSec);
		if (Result == MLResult_Ok)
		{
			Duration = FTimespan::FromMilliseconds(durationMilSec);
		}
		else
		{
			UE_LOG(LogMagicLeapMedia, Error, TEXT("MLMediaPlayerGetDuration failed with error %s"), UTF8_TO_TCHAR(MLMediaResultGetString(Result)));
		}
	}

	return Duration;
}

float FMagicLeapMediaPlayer::GetRate() const
{
	return (CurrentState == EMediaState::Playing) ? 1.0f : 0.0f;
}

EMediaState FMagicLeapMediaPlayer::GetState() const
{
	return CurrentState;
}

EMediaStatus FMagicLeapMediaPlayer::GetStatus() const
{
	return EMediaStatus::None;
}

TRangeSet<float> FMagicLeapMediaPlayer::GetSupportedRates(EMediaRateThinning Thinning) const
{
	TRangeSet<float> Result;

	Result.Add(TRange<float>(0.0f));
	Result.Add(TRange<float>(1.0f));

	return Result;
}

FTimespan FMagicLeapMediaPlayer::GetTime() const
{
	return CurrentPlaybackTime.Load();
}

bool FMagicLeapMediaPlayer::IsLooping() const
{
	return GetMediaPlayerState(MLMediaPlayerPollingStateFlag_IsLooping);
}

bool FMagicLeapMediaPlayer::Seek(const FTimespan& Time)
{
	bool bSuccess = true;

	if ((CurrentState == EMediaState::Closed) || (CurrentState == EMediaState::Error) || (CurrentState == EMediaState::Preparing))
	{
		bSuccess = false;
		UE_LOG(LogMagicLeapMedia, Warning, TEXT("Cannot seek while closed, preparing, or in error state"));
	}
	else if (CurrentState == EMediaState::Playing || CurrentState == EMediaState::Paused)
	{
		MLResult Result = MLMediaPlayerSeekTo(MediaPlayerHandle, static_cast<int32>(Time.GetTotalMilliseconds()), MLMediaSeekMode_Closest_Sync);
		if (Result != MLResult_Ok)
		{
			bSuccess = false;
			UE_LOG(LogMagicLeapMedia, Error, TEXT("MLMediaPlayerSeekTo failed with error %s"), UTF8_TO_TCHAR(MLMediaResultGetString(Result)));
		}
	}

	return bSuccess;
}

bool FMagicLeapMediaPlayer::SetLooping(bool Looping)
{
	MLResult Result = MLMediaPlayerSetLooping(MediaPlayerHandle, Looping);
	if (Result != MLResult_Ok)
	{
		UE_LOG(LogMagicLeapMedia, Error, TEXT("MLMediaPlayerSetLooping failed with error %s"), UTF8_TO_TCHAR(MLMediaResultGetString(Result)));
		return false;
	}

	return true;
}

bool FMagicLeapMediaPlayer::SetRate(float Rate)
{
	if ((CurrentState == EMediaState::Closed) || (CurrentState == EMediaState::Error) || (CurrentState == EMediaState::Preparing))
	{
		UE_LOG(LogMagicLeapMedia, Warning, TEXT("Cannot set rate while closed, preparing, or in error state"));
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
		MLResult Result = MLMediaPlayerPause(MediaPlayerHandle);
		if (Result == MLResult_Ok)
		{
			CurrentState = EMediaState::Paused;
			EventSink.ReceiveMediaEvent(EMediaEvent::PlaybackSuspended);
		}
		else
		{
			UE_LOG(LogMagicLeapMedia, Error, TEXT("MLMediaPlayerPause failed with error %s!"), UTF8_TO_TCHAR(MLMediaResultGetString(Result)));
		}
	}
	else if (Rate == 1.0f)
	{
		MLResult Result = MLMediaPlayerStart(MediaPlayerHandle);
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogMagicLeapMedia, Error, TEXT("MLMediaPlayerStart failed with error %s"), UTF8_TO_TCHAR(MLMediaResultGetString(Result)));
			bResult = false;
		}

		CurrentState = EMediaState::Playing;
		EventSink.ReceiveMediaEvent(EMediaEvent::PlaybackResumed);
		bResult = true;
	}
	else
	{
		UE_LOG(LogMagicLeapMedia, Error, TEXT("Rate %f not supported by MagicLeapMedia."), Rate);
		bResult = false;
	}

	return bResult;
}

bool FMagicLeapMediaPlayer::SetNativeVolume(float Volume)
{
	bool bSuccess = true;

	if (MLHandleIsValid(MediaPlayerHandle))
	{
		Volume = (Volume < 0.0f) ? 0.0f : ((Volume < 1.0f) ? Volume : 1.0f);
		MLResult Result = MLMediaPlayerSetVolume(MediaPlayerHandle, Volume);
		if (Result != MLResult_Ok)
		{
			bSuccess = false;
			UE_LOG(LogMagicLeapMedia, Error, TEXT("MLMediaPlayerSetVolume failed with error %s."), UTF8_TO_TCHAR(MLMediaResultGetString(Result)));
		}
	}

	return bSuccess;
}

void FMagicLeapMediaPlayer::SetGuid(const FGuid& Guid)
{
	PlayerGuid = Guid;
}

void FMagicLeapMediaPlayer::TickFetch(FTimespan DeltaTime, FTimespan Timecode)
{
	if (CurrentState != EMediaState::Playing && CurrentState != EMediaState::Paused)
	{
		return;
	}

	int32 currentPositionMilSec = 0;
	MLResult Result = MLMediaPlayerGetCurrentPosition(MediaPlayerHandle, &currentPositionMilSec);
	if (Result == MLResult_Ok)
	{
		CurrentPlaybackTime = FTimespan::FromMilliseconds(currentPositionMilSec);
	}
	else
	{
		UE_LOG(LogMagicLeapMedia, Error, TEXT("MLMediaPlayerGetCurrentPosition failed with error %s"), UTF8_TO_TCHAR(MLMediaResultGetString(Result)));
	}

	// deal with resolution changes (usually from streams)
	if (GetMediaPlayerState(MLMediaPlayerPollingStateFlag_HasSizeChanged))
	{
		FIntPoint Dimensions = FIntPoint(0, 0);
		TextureData->bIsVideoTextureValid = false;
	}

	if (GSupportsImageExternal)
	{
		if (FLuminPlatformMisc::ShouldUseVulkan())
		{
			struct FWriteVideoSampleParams
			{
				FMagicLeapMediaPlayer* MediaPlayer;
				TWeakPtr<FMagicLeapVideoTextureDataVK, ESPMode::ThreadSafe> TextureData;
				FGuid PlayerGuid;
				MLHandle MediaPlayerHandle;
			};

			FWriteVideoSampleParams Params = { this, StaticCastSharedPtr<FMagicLeapVideoTextureDataVK>(TextureData), PlayerGuid, MediaPlayerHandle };

			ENQUEUE_RENDER_COMMAND(MagicLeapMediaPlayerWriteVideoSample)(
				[Params](FRHICommandListImmediate& RHICmdList)
				{
					auto TextureDataPtr = Params.TextureData.Pin();
			
					if (!Params.MediaPlayer->RenderThreadIsBufferAvailable(Params.MediaPlayerHandle))
					{
						return;
					}

					if (TextureDataPtr->PreviousNativeBuffer != 0 && MLHandleIsValid(TextureDataPtr->PreviousNativeBuffer))
					{
						Params.MediaPlayer->RenderThreadReleaseNativeBuffer(Params.MediaPlayerHandle, TextureDataPtr->PreviousNativeBuffer);
						TextureDataPtr->PreviousNativeBuffer = 0;
					}

					MLHandle NativeBuffer = ML_INVALID_HANDLE;
					if (!Params.MediaPlayer->RenderThreadGetNativeBuffer(Params.MediaPlayerHandle, NativeBuffer, TextureDataPtr->bIsVideoTextureValid))
					{
						return;
					}

					check(MLHandleIsValid(NativeBuffer));

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
							UE_LOG(LogMagicLeapMedia, Error, TEXT("Failed to get next media texture."));
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
				FMagicLeapMediaPlayer* MediaPlayer;
				TWeakPtr<FMagicLeapVideoTextureDataGL, ESPMode::ThreadSafe> TextureData;
				FGuid PlayerGuid;
				MLHandle MediaPlayerHandle;
			};

			FWriteVideoSampleParams Params = { this, StaticCastSharedPtr<FMagicLeapVideoTextureDataGL>(TextureData), PlayerGuid, MediaPlayerHandle };

			ENQUEUE_RENDER_COMMAND(MagicLeapMediaPlayerWriteVideoSample)(
				[Params](FRHICommandListImmediate& RHICmdList)
				{
					auto TextureDataPtr = Params.TextureData.Pin();

					if (!Params.MediaPlayer->RenderThreadIsBufferAvailable(Params.MediaPlayerHandle))
					{
						return;
					}

					FTextureRHIRef MediaVideoTexture = TextureDataPtr->VideoTexture;
					if (MediaVideoTexture == nullptr)
					{
						FRHIResourceCreateInfo CreateInfo;
						MediaVideoTexture = RHICmdList.CreateTextureExternal2D(1, 1, PF_R8G8B8A8, 1, 1, 0, CreateInfo);
						TextureDataPtr->VideoTexture = MediaVideoTexture;

						if (MediaVideoTexture == nullptr)
						{
							UE_LOG(LogMagicLeapMedia, Warning, TEXT("CreateTextureExternal2D failed!"));
							return;
						}

						TextureDataPtr->bIsVideoTextureValid = false;
					}

					// MLHandle because Unreal's uint64 is 'unsigned long long *' whereas uint64_t for the C-API is 'unsigned long *'
					// TODO: Fix the Unreal types for the above comment.
					MLHandle nativeBuffer = ML_INVALID_HANDLE;
					if (!Params.MediaPlayer->RenderThreadGetNativeBuffer(Params.MediaPlayerHandle, nativeBuffer, TextureDataPtr->bIsVideoTextureValid))
					{
						return;
					}

					int32 CurrentFramePosition = 0;
					if (!Params.MediaPlayer->RenderThreadGetCurrentPosition(Params.MediaPlayerHandle, CurrentFramePosition))
					{
						return;
					}

					int32 TextureID = *reinterpret_cast<int32*>(MediaVideoTexture->GetNativeResource());

					TextureDataPtr->TextureRenderer.DestroyImageKHR();

					if (TextureDataPtr->PreviousNativeBuffer != 0 && MLHandleIsValid(TextureDataPtr->PreviousNativeBuffer))
					{
						Params.MediaPlayer->RenderThreadReleaseNativeBuffer(Params.MediaPlayerHandle, TextureDataPtr->PreviousNativeBuffer);
					}
					TextureDataPtr->PreviousNativeBuffer = nativeBuffer;

					if (TextureDataPtr->TextureRenderer.CreateImageKHR(reinterpret_cast<void*>(nativeBuffer)))
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

	if (GetMediaPlayerState(MLMediaPlayerPollingStateFlag_HasSubtitleUpdated))
	{
		MLCea608CaptionSegment Segment;
		MLCea608CaptionSegmentInit(&Segment);
		MLMediaPlayerSubtitleData* SubtitleDataPtr = nullptr;

		ProcessCaptioningSegment(Segment, SubtitleDataPtr);

		MLResult ReleaseSegmentResult = MLMediaCCParserReleaseSegment(CaptionParserHandle, &Segment);
		UE_CLOG(ReleaseSegmentResult != MLResult_Ok, LogMagicLeapMedia, Error, TEXT("MLMediaCCParserReleaseSegment failed with error %s."), UTF8_TO_TCHAR(MLGetResultString(ReleaseSegmentResult)));

		MLResult ReleaseSubtitleResult = MLMediaPlayerReleaseSubtitleEx(MediaPlayerHandle);
		UE_CLOG(ReleaseSubtitleResult != MLResult_Ok, LogMagicLeapMedia, Error, TEXT("MLMediaPlayerReleaseSubtitleEx failed with error %s."), UTF8_TO_TCHAR(MLGetResultString(ReleaseSubtitleResult)));
	}
}

void FMagicLeapMediaPlayer::RegisterExternalTexture(const FGuid& InGuid, FTextureRHIRef& InTextureRHI, FSamplerStateRHIRef& InSamplerStateRHI)
{
	FExternalTextureRegistry::Get().RegisterExternalTexture(InGuid, InTextureRHI, InSamplerStateRHI, FLinearColor(UScale, 0.0f, 0.0f, VScale), FLinearColor(UOffset,  VOffset, 0.0f, 0.0f));
}

void FMagicLeapMediaPlayer::TickInput(FTimespan DeltaTime, FTimespan Timecode)
{
	if (!bMediaPrepared)
	{
		bMediaPrepared = GetMediaPlayerState(MLMediaPlayerPollingStateFlag_HasBeenPrepared);

		if (bMediaPrepared)
		{
			CurrentState = EMediaState::Stopped;

			TrackInfo.Add(EMediaTrackType::Video);
			TrackInfo.Add(EMediaTrackType::Audio);
			TrackInfo.Add(EMediaTrackType::Caption);
			TrackInfo.Add(EMediaTrackType::Subtitle);
			TrackInfo.Add(EMediaTrackType::Metadata);

			SelectedTrack.Add(EMediaTrackType::Video, INDEX_NONE);
			SelectedTrack.Add(EMediaTrackType::Audio, INDEX_NONE);
			SelectedTrack.Add(EMediaTrackType::Caption, INDEX_NONE);
			SelectedTrack.Add(EMediaTrackType::Subtitle, INDEX_NONE);
			SelectedTrack.Add(EMediaTrackType::Metadata, INDEX_NONE);

			uint32 NumTracks = 0;
			MLMediaPlayerGetTrackCount(MediaPlayerHandle, &NumTracks);
			for (uint32 i = 0; i < NumTracks; ++i)
			{
				MLMediaPlayerTrackType TrackType;
				MLMediaPlayerGetTrackType(MediaPlayerHandle, i, &TrackType);
				switch(TrackType)
				{
					case MediaPlayerTrackType_Video:
						TrackInfo[EMediaTrackType::Video].Add(static_cast<int32>(i));
						SelectedTrack[EMediaTrackType::Video] = 0;
						break;
					case MediaPlayerTrackType_Audio:
						TrackInfo[EMediaTrackType::Audio].Add(static_cast<int32>(i));
						SelectedTrack[EMediaTrackType::Audio] = 0;
						break;
					case MediaPlayerTrackType_TimedText:
						TrackInfo[EMediaTrackType::Caption].Add(static_cast<int32>(i));
						SelectedTrack[EMediaTrackType::Caption] = 0;
						break;
					case MediaPlayerTrackType_Subtitle:
						TrackInfo[EMediaTrackType::Subtitle].Add(static_cast<int32>(i));
						SelectedTrack[EMediaTrackType::Subtitle] = 0;
						break;
					case MediaPlayerTrackType_Metadata:
						TrackInfo[EMediaTrackType::Metadata].Add(static_cast<int32>(i));
						SelectedTrack[EMediaTrackType::Metadata] = 0;
						break;
				}
			}
			// notify listeners
			EventSink.ReceiveMediaEvent(EMediaEvent::TracksChanged);
			EventSink.ReceiveMediaEvent(EMediaEvent::MediaOpened);
		}
		else
		{
			return;
		}
	}

	// Subtitle/Caption tracks are only picked up on MetadataUpdated
	if (GetMediaPlayerState(MLMediaPlayerPollingStateFlag_HasMetadataUpdated))
	{
		CheckSubtitlesAndCaptioning();
		if (!MLHandleIsValid(CaptionParserHandle))
		{
			MLResult Result = MLMediaCCParserCreate(&CaptionParserHandle);
			UE_CLOG(Result != MLResult_Ok, LogMagicLeapMedia, Error, TEXT("MLMediaCCParserCreate failed with error %s."), UTF8_TO_TCHAR(MLGetResultString(Result)));
		}
	}

	if (GetMediaPlayerState(MLMediaPlayerPollingStateFlag_HasSeekCompleted))
	{
		EventSink.ReceiveMediaEvent(EMediaEvent::SeekCompleted);
	}

	if (CurrentState != EMediaState::Playing)
	{
		return;
	}

	if (GetMediaPlayerState(MLMediaPlayerPollingStateFlag_HasPlaybackCompleted))
	{
		{
			FScopeLock Lock(&CriticalSection);
			bPlaybackCompleted = true;
		}
		if (!IsLooping())
		{
			CurrentState = EMediaState::Stopped;
		}
		EventSink.ReceiveMediaEvent(EMediaEvent::PlaybackEndReached);
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
	}

	// register delegate if not registered
	if (!ResumeHandle.IsValid())
	{
		ResumeHandle = FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddRaw(this, &FMagicLeapMediaPlayer::HandleApplicationHasEnteredForeground);
	}
	if (!PauseHandle.IsValid())
	{
		PauseHandle = FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddRaw(this, &FMagicLeapMediaPlayer::HandleApplicationWillEnterBackground);
	}
}

bool FMagicLeapMediaPlayer::GetAudioTrackFormat(int32 TrackIndex, int32 FormatIndex, FMediaAudioTrackFormat& OutFormat) const
{
	if ((FormatIndex != static_cast<int32>(EMediaTrackType::Audio)) || TrackIndex >= TrackInfo[EMediaTrackType::Audio].Num() || TrackIndex < 0)
	{
		return false;
	}

	MLHandle FormatHandle;
	MLResult Result = MLMediaPlayerGetTrackMediaFormat(MediaPlayerHandle, static_cast<uint32>(TrackInfo[EMediaTrackType::Audio][TrackIndex]), &FormatHandle);
	if (Result != MLResult_Ok)
	{
		UE_LOG(LogMagicLeapMedia, Error, TEXT("MLMediaPlayerGetTrackMediaFormat failed with error %s"), UTF8_TO_TCHAR(MLMediaResultGetString(Result)));
		return false;
	}

	//Update integer format values
	Result = MLMediaFormatGetKeyValueInt32(FormatHandle, MLMediaFormat_Key_Sample_Rate, &OutFormat.SampleRate);
	UE_CLOG(Result != MLResult_Ok, LogMagicLeapMedia, Error, TEXT("MLMediaFormatGetKeyValueInt32(sample-rate) failed with error %s"), UTF8_TO_TCHAR(MLMediaResultGetString(Result)));
	Result = MLMediaFormatGetKeyValueInt32(FormatHandle, MLMediaFormat_Key_Channel_Count, &OutFormat.NumChannels);
	UE_CLOG(Result != MLResult_Ok, LogMagicLeapMedia, Error, TEXT("MLMediaFormatGetKeyValueInt32(channel-count) failed with error %s"), UTF8_TO_TCHAR(MLMediaResultGetString(Result)));
	Result = MLMediaFormatGetKeyValueInt32(FormatHandle, MLMediaFormat_Key_Bit_Rate, &OutFormat.BitsPerSample);
	UE_CLOG(Result != MLResult_Ok, LogMagicLeapMedia, Error, TEXT("MLMediaFormatGetKeyValueInt32(bit-rate) failed with error %s"), UTF8_TO_TCHAR(MLMediaResultGetString(Result)));

	//Update string format value, removing the format type.
	char FormatName[MAX_FORMAT_STRING_SIZE] = "";
	Result = MLMediaFormatGetKeyString(FormatHandle, MLMediaFormat_Key_Mime, &FormatName);
	UE_CLOG(Result != MLResult_Ok, LogMagicLeapMedia, Error, TEXT("MLMediaFormatGetKeyString(mime) failed with error %s"), UTF8_TO_TCHAR(MLMediaResultGetString(Result)));
	OutFormat.TypeName = (Result == MLResult_Ok) ? FString(FormatName) : FString();
	OutFormat.TypeName.RemoveFromStart("audio/");

	MLMediaFormatDestroy(FormatHandle);

	return true;
}

int32 FMagicLeapMediaPlayer::GetNumTracks(EMediaTrackType TrackType) const
{
	if (TrackInfo.Contains(TrackType))
	{
		return TrackInfo[TrackType].Num();
	}

	return 0;
}

int32 FMagicLeapMediaPlayer::GetNumTrackFormats(EMediaTrackType TrackType, int32 TrackIndex) const
{
	return ((TrackIndex >= 0) && (TrackIndex < GetNumTracks(TrackType))) ? 1 : 0;
}

int32 FMagicLeapMediaPlayer::GetSelectedTrack(EMediaTrackType TrackType) const
{
	if (SelectedTrack.Contains(TrackType))
	{
		return SelectedTrack[TrackType];
	}

	return INDEX_NONE;
}

FText FMagicLeapMediaPlayer::GetTrackDisplayName(EMediaTrackType TrackType, int32 TrackIndex) const
{
	return FText::GetEmpty();
}

int32 FMagicLeapMediaPlayer::GetTrackFormat(EMediaTrackType TrackType, int32 TrackIndex) const
{
	return (GetSelectedTrack(TrackType) != INDEX_NONE) ? 0 : INDEX_NONE;
}

FString FMagicLeapMediaPlayer::GetTrackLanguage(EMediaTrackType TrackType, int32 TrackIndex) const
{
	if (TrackInfo.Contains(TrackType) && TrackIndex >= 0 && TrackIndex < TrackInfo[TrackType].Num())
	{
		char* TrackLanguage = nullptr;
		MLResult Result = MLMediaPlayerGetTrackLanguage(MediaPlayerHandle, static_cast<uint32>(TrackInfo[TrackType][TrackIndex]), &TrackLanguage);
		if (Result == MLResult_Ok)
		{
			FString Language(UTF8_TO_TCHAR(TrackLanguage));
			free(TrackLanguage);
			return Language;
		}
		else
		{
			UE_LOG(LogMagicLeapMedia, Error, TEXT("MLMediaPlayerGetTrackLanguage failed with error %s"), UTF8_TO_TCHAR(MLMediaResultGetString(Result)));
		}
	}

	return FString();
}

FString FMagicLeapMediaPlayer::GetTrackName(EMediaTrackType TrackType, int32 TrackIndex) const
{
	// Track names not supported in ML.
	return FString();
}

bool FMagicLeapMediaPlayer::GetVideoTrackFormat(int32 TrackIndex, int32 FormatIndex, FMediaVideoTrackFormat& OutFormat) const
{
	if ((FormatIndex != 0) || TrackIndex >= TrackInfo[EMediaTrackType::Video].Num())
	{
		return false;
	}

	int32 width = 0;
	int32 height = 0;
	MLResult Result = MLMediaPlayerGetVideoSize(MediaPlayerHandle, &width, &height);
	if (Result != MLResult_Ok)
	{
		UE_LOG(LogMagicLeapMedia, Error, TEXT("MLMediaPlayerGetVideoSize failed with error %s"), UTF8_TO_TCHAR(MLMediaResultGetString(Result)));
		return false;
	}

	MLHandle MediaFormatHandle = ML_INVALID_HANDLE;
	Result = MLMediaPlayerGetTrackMediaFormat(MediaPlayerHandle, TrackIndex, &MediaFormatHandle);
	if (Result != MLResult_Ok)
	{
		UE_LOG(LogMagicLeapMedia, Error, TEXT("MLMediaPlayerGetTrackMediaFormat failed with error %s"), UTF8_TO_TCHAR(MLMediaResultGetString(Result)));
		return false;
	}

	int32 FrameRate = 0;
	MLMediaFormatGetKeyValueInt32(MediaFormatHandle, MLMediaFormat_Key_Frame_Rate, &FrameRate);

	if (Result != MLResult_Ok)
	{
		UE_LOG(LogMagicLeapMedia, Error, TEXT("MLMediaFormatGetKeyValueInt32 failed with error %s"), UTF8_TO_TCHAR(MLMediaResultGetString(Result)));
		return false;
	}
	OutFormat.FrameRate = static_cast<float>(FrameRate);
	OutFormat.FrameRates = TRange<float>(FrameRate);

	char MIME[MAX_KEY_STRING_SIZE] = "";
	Result = MLMediaFormatGetKeyString(MediaFormatHandle, MLMediaFormat_Key_Mime, &MIME);
	if (Result != MLResult_Ok)
	{
		UE_LOG(LogMagicLeapMedia, Error, TEXT("MLMediaFormatGetKeyString failed with error %s"), UTF8_TO_TCHAR(MLMediaResultGetString(Result)));
		return false;
	}
	OutFormat.TypeName = UTF8_TO_TCHAR(MIME);

	return true;
}

bool FMagicLeapMediaPlayer::SelectTrack(EMediaTrackType TrackType, int32 TrackIndex)
{
	if (TrackInfo.Contains(TrackType) && CurrentState != EMediaState::Preparing)
	{
		if (TrackInfo[TrackType].IsValidIndex(TrackIndex))
		{
			MLResult Result = MLMediaPlayerSelectTrack(MediaPlayerHandle, static_cast<uint32>(TrackInfo[TrackType][TrackIndex]));
			if (Result == MLResult_Ok)
			{
				SelectedTrack[TrackType] = TrackIndex;
				return true;
			}
			else
			{
				UE_LOG(LogMagicLeapMedia, Error, TEXT("MLMediaPlayerSelectTrack failed with error %s"), UTF8_TO_TCHAR(MLMediaResultGetString(Result)));
			}
		}
	}
	return false;
}

bool FMagicLeapMediaPlayer::SetTrackFormat(EMediaTrackType TrackType, int32 TrackIndex, int32 FormatIndex)
{
	return false;
}

void FMagicLeapMediaPlayer::HandleApplicationHasEnteredForeground()
{
	// check state in case changed before ticked
	if (CurrentState == EMediaState::Paused && bWasMediaPlayingBeforeAppPause)
	{
		// Unpause
		SetRate(1.0f);
	}
}

void FMagicLeapMediaPlayer::HandleApplicationWillEnterBackground()
{
	bWasMediaPlayingBeforeAppPause = (CurrentState == EMediaState::Playing);
	// check state in case changed before ticked
	if (bWasMediaPlayingBeforeAppPause)
	{
		// pause
		SetRate(0.0f);
	}
}

void FMagicLeapMediaPlayer::CheckSubtitlesAndCaptioning()
{
	uint32 NumTracks = 0;
	MLMediaPlayerGetTrackCount(MediaPlayerHandle, &NumTracks);
	for (uint32 i = 0; i < NumTracks; ++i)
	{
		MLMediaPlayerTrackType TrackType = MediaPlayerTrackType_Unknown;
		MLMediaPlayerGetTrackType(MediaPlayerHandle, i, &TrackType);
		switch (TrackType)
		{
		case MediaPlayerTrackType_TimedText:
			if (!TrackInfo[EMediaTrackType::Caption].Contains(static_cast<int32>(i)))
			{
				TrackInfo[EMediaTrackType::Caption].Add(static_cast<int32>(i));
			}
			if (SelectedTrack[EMediaTrackType::Caption] == INDEX_NONE)
			{
				SelectedTrack[EMediaTrackType::Caption] = 0;
				SelectTrack(EMediaTrackType::Caption, SelectedTrack[EMediaTrackType::Caption]);
			}
			break;
		case MediaPlayerTrackType_Subtitle:
			if (!TrackInfo[EMediaTrackType::Subtitle].Contains(static_cast<int32>(i)))
			{
				TrackInfo[EMediaTrackType::Subtitle].Add(static_cast<int32>(i));
			}
			if (SelectedTrack[EMediaTrackType::Subtitle] == INDEX_NONE)
			{
				SelectedTrack[EMediaTrackType::Subtitle] = 0;
				SelectTrack(EMediaTrackType::Subtitle, SelectedTrack[EMediaTrackType::Subtitle]);
			}
			break;
		// We currently ignore other track types at detected metadata changes
		default:
			break;
		}
	}
}

void FMagicLeapMediaPlayer::ProcessCaptioningSegment(MLCea608CaptionSegment& Segment, MLMediaPlayerSubtitleData* SubtitleDataPtr)
{
	check(MLHandleIsValid(CaptionParserHandle));
	MLResult Result = MLMediaPlayerGetSubtitleEx(MediaPlayerHandle, &SubtitleDataPtr);
	if (Result == MLResult_Ok)
	{
		MLResult DisplayableResult = MLMediaCCParserGetDisplayable(CaptionParserHandle, SubtitleDataPtr->data, SubtitleDataPtr->data_size, &Segment);
		if (DisplayableResult != MLResult_Ok)
		{
			UE_LOG(LogMagicLeapMedia, Error, TEXT("MLMediaCCParserGetDisplayable failed with error %s."), UTF8_TO_TCHAR(MLGetResultString(DisplayableResult)));
			return;
		}

		for (uint32 Index = 0; Index < MLCea608_CCMaxRowsPlus2; ++Index)
		{
			if (Segment.ccLine[Index] != nullptr)
			{
				TArray<char> LineCharacters;
				LineCharacters.Reserve(MLCea608_CCMaxColsPlus2);
				// Caption lines are kept in their respective row and column due to style data corresponding
				// to row/column. So we must check for non-characters in the string
				for (uint32 LineIndex = 0; LineIndex < MLCea608_CCMaxColsPlus2; ++LineIndex)
				{
					if (Segment.ccLine[Index]->displayChars[LineIndex] != '\xff')
					{
						LineCharacters.Emplace(Segment.ccLine[Index]->displayChars[LineIndex]);
					}
				}
				TSharedRef<FMagicLeapMediaOverlaySample, ESPMode::ThreadSafe> Subtitle = MakeShared<FMagicLeapMediaOverlaySample, ESPMode::ThreadSafe>();
				Subtitle->Intitialize(LineCharacters.GetData(), EMediaOverlaySampleType::Subtitle, FTimespan::FromMicroseconds(SubtitleDataPtr->duration_us), FTimespan::FromMicroseconds(SubtitleDataPtr->start_time_us));
				Samples->AddSubtitle(Subtitle);
			}
		}
	}
	else
	{
		UE_LOG(LogMagicLeapMedia, Error, TEXT("MLMediaPlayerGetSubtitleEx failed with error %s."), UTF8_TO_TCHAR(MLGetResultString(Result)));
	}
}

FIntPoint FMagicLeapMediaPlayer::GetVideoDimensions() const
{
	int32 width = 0;
	int32 height = 0;
	MLResult Result = MLMediaPlayerGetVideoSize(MediaPlayerHandle, &width, &height);
	if (Result != MLResult_Ok)
	{
		UE_LOG(LogMagicLeapMedia, Error, TEXT("MLMediaPlayerGetVideoSize failed with error %s"), UTF8_TO_TCHAR(MLMediaResultGetString(Result)));
	}
	return FIntPoint(width, height);
}

bool FMagicLeapMediaPlayer::GetMediaPlayerState(uint16 FlagToPoll) const
{
	uint16_t StateFlags = 0;
	MLResult Result = MLMediaPlayerPollStates(MediaPlayerHandle, FlagToPoll, &StateFlags);
	if (Result != MLResult_Ok)
	{
		UE_LOG(LogMagicLeapMedia, Error, TEXT("MLMediaPlayerPollStates failed with error %s"), UTF8_TO_TCHAR(MLMediaResultGetString(Result)));
		return false;
	}

	return FlagToPoll & StateFlags;
}

bool FMagicLeapMediaPlayer::RenderThreadIsBufferAvailable(MLHandle InMediaPlayerHandle)
{
	ensureMsgf(IsInRenderingThread(), TEXT("RenderThreadIsBufferAvailable called outside of render thread"));
	uint16_t StateFlags = 0;
	MLResult Result = MLMediaPlayerPollStates(InMediaPlayerHandle, MLMediaPlayerPollingStateFlag_IsBufferAvailable, &StateFlags);
	if (Result != MLResult_Ok)
	{
		UE_LOG(LogMagicLeapMedia, Error, TEXT("MLMediaPlayerPollStates failed with error %s"), UTF8_TO_TCHAR(MLMediaResultGetString(Result)));
		return false;
	}

	return MLMediaPlayerPollingStateFlag_IsBufferAvailable & StateFlags;
}

bool FMagicLeapMediaPlayer::RenderThreadGetNativeBuffer(const MLHandle InMediaPlayerHandle, MLHandle& NativeBuffer, bool& OutIsVideoTextureValid)
{
	ensureMsgf(IsInRenderingThread(), TEXT("RenderThreadGetNativeBuffer called outside of render thread"));
	MLResult Result = MLMediaPlayerAcquireNextAvailableBuffer(InMediaPlayerHandle, &NativeBuffer);
	if (Result != MLResult_Ok)
	{
		UE_LOG(LogMagicLeapMedia, Error, TEXT("MLMediaPlayerAcquireNextAvailableBuffer failed with error %s"), UTF8_TO_TCHAR(MLMediaResultGetString(Result)));
		return false;
	}

	Result = MLMediaPlayerGetFrameTransformationMatrix(InMediaPlayerHandle, FrameTransformationMatrix);
	if (Result != MLResult_Ok)
	{
		UE_LOG(LogMagicLeapMedia, Error, TEXT("MLMediaPlayerGetFrameTransformationMatrix failed with error %s"), UTF8_TO_TCHAR(MLMediaResultGetString(Result)));
		return false;
	}

	if (UScale != FrameTransformationMatrix[0] ||
		UOffset != FrameTransformationMatrix[12] ||
		(-VScale) != FrameTransformationMatrix[5] /*||
		(1.0f - VOffset) != FrameTransformationMatrix[13]*/)
	{
		UScale = FrameTransformationMatrix[0];
		UOffset = FrameTransformationMatrix[12];
		VScale = -FrameTransformationMatrix[5];
		//VOffset = 1.0f - FrameTransformationMatrix[13];
		OutIsVideoTextureValid = false;
	}

	return true;
}

bool FMagicLeapMediaPlayer::RenderThreadReleaseNativeBuffer(const MLHandle InMediaPlayerHandle, MLHandle NativeBuffer)
{
	ensureMsgf(IsInRenderingThread(), TEXT("RenderThreadReleaseNativeBuffer called outside of render thread"));
	MLResult Result = MLMediaPlayerReleaseBuffer(InMediaPlayerHandle, NativeBuffer);
	if (Result != MLResult_Ok)
	{
		UE_LOG(LogMagicLeapMedia, Error, TEXT("MLMediaPlayerReleaseBuffer failed with error %s"), UTF8_TO_TCHAR(MLMediaResultGetString(Result)));
		return false;
	}

	return true;
}

bool FMagicLeapMediaPlayer::RenderThreadGetCurrentPosition(const MLHandle InMediaPlayerHandle, int32& CurrentPosition)
{
	ensureMsgf(IsInRenderingThread(), TEXT("RenderThreadGetCurrentPosition called outside of render thread"));
	MLResult Result = MLMediaPlayerGetCurrentPosition(InMediaPlayerHandle, &CurrentPosition);
	if (Result != MLResult_Ok)
	{
		UE_LOG(LogMagicLeapMedia, Error, TEXT("MLMediaPlayerGetCurrentPosition failed with error %s"), UTF8_TO_TCHAR(MLMediaResultGetString(Result)));
		return false;
	}

	return true;
}

void FMagicLeapMediaPlayer::TriggerResetAndDestroy()
{
	// Platform api for MLMediaPlayerReset()/Destroy() has a bug where the function can stall for several seconds.
	// Until that is fixed, we call those functions on a detached thread so as not to block the app's process from exiting.

	int32 ThreadFuncResult = 0;
	pthread_t Thread;
	pthread_attr_t attr;

	ThreadFuncResult = pthread_attr_init(&attr);
	if (ThreadFuncResult == 0)
	{
		ThreadFuncResult = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		if (ThreadFuncResult == 0)
		{
			ThreadFuncResult = pthread_create(&Thread, &attr, _ThreadProc, reinterpret_cast<void*>(MediaPlayerHandle));
			UE_CLOG(ThreadFuncResult != 0, LogMagicLeapMedia, Error, TEXT("pthread_create failed with error %d."), ThreadFuncResult);
		}
		else
		{
			UE_LOG(LogMagicLeapMedia, Error, TEXT("pthread_attr_setdetachstate failed with error %d."), ThreadFuncResult);
		}
	}
	else
	{
		UE_LOG(LogMagicLeapMedia, Error, TEXT("pthread_attr_init failed with error %d."), ThreadFuncResult);
	}
}

#undef LOCTEXT_NAMESPACE
