// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapMusicServiceFunctionLibrary.h"
#include "MagicLeapMusicServicePlugin.h"
#include "Misc/Timespan.h"
#include "Lumin/CAPIShims/LuminAPIMusicService.h"
#include "Async/Async.h"

DEFINE_LOG_CATEGORY_STATIC(LogMusicServiceFunctionLibrary, Display, All);

#if WITH_MLSDK
FMagicLeapMusicServiceTrackMetadata MLToUnrealServiceMetadata(const MLMusicServiceMetadata& MLMetadata)
{
	FMagicLeapMusicServiceTrackMetadata Metadata;

	Metadata.TrackTitle = UTF8_TO_TCHAR(MLMetadata.track_title);
	Metadata.AlbumName = UTF8_TO_TCHAR(MLMetadata.album_name);
	Metadata.AlbumURL = UTF8_TO_TCHAR(MLMetadata.album_url);
	Metadata.AlbumCoverURL = UTF8_TO_TCHAR(MLMetadata.album_cover_url);
	Metadata.ArtistName = UTF8_TO_TCHAR(MLMetadata.artist_name);
	Metadata.ArtistURL = UTF8_TO_TCHAR(MLMetadata.artist_url);
	Metadata.TrackLength = FTimespan::FromSeconds(static_cast<double>(MLMetadata.length));

	return Metadata;
}

EMagicLeapMusicServiceProviderError MLToUnrealServiceErrorType(const MLMusicServiceError& InError)
{
	switch (InError)
	{
	case MLMusicServiceError_None:
		return EMagicLeapMusicServiceProviderError::None;
	case MLMusicServiceError_Connectivity:
		return EMagicLeapMusicServiceProviderError::Connectivity;
	case MLMusicServiceError_Timeout:
		return EMagicLeapMusicServiceProviderError::Timeout;
	case MLMusicServiceError_GeneralPlayback:
		return EMagicLeapMusicServiceProviderError::GeneralPlayback;
	case MLMusicServiceError_Privilege:
		return EMagicLeapMusicServiceProviderError::Privilege;
	case MLMusicServiceError_ServiceSpecific:
		return EMagicLeapMusicServiceProviderError::ServiceSpecific;
	case MLMusicServiceError_NoMemory:
		return EMagicLeapMusicServiceProviderError::NoMemory;
	case MLMusicServiceError_Unspecified:
		return EMagicLeapMusicServiceProviderError::Unspecified;
	default:
		return EMagicLeapMusicServiceProviderError::Unspecified;
	}
}

EMagicLeapMusicServicePlaybackState MLToUnrealPlaybackState(const MLMusicServicePlaybackState& InState)
{
	switch (InState)
	{
	case MLMusicServicePlaybackState_Playing:
		return EMagicLeapMusicServicePlaybackState::Playing;
	case MLMusicServicePlaybackState_Paused:
		return EMagicLeapMusicServicePlaybackState::Paused;
	case MLMusicServicePlaybackState_Stopped:
		return EMagicLeapMusicServicePlaybackState::Stopped;
	case MLMusicServicePlaybackState_Error:
		return EMagicLeapMusicServicePlaybackState::Error;
	case MLMusicServicePlaybackState_Unknown:
		return EMagicLeapMusicServicePlaybackState::Unknown;
	default:
		return EMagicLeapMusicServicePlaybackState::Unknown;
	}
}

MLMusicServiceShuffleState UnrealToMLShuffleState(const EMagicLeapMusicServicePlaybackShuffleState& InState)
{
	switch (InState)
	{
	case EMagicLeapMusicServicePlaybackShuffleState::On:
		return MLMusicServiceShuffleState_On;
	case EMagicLeapMusicServicePlaybackShuffleState::Off:
		return MLMusicServiceShuffleState_Off;
	case EMagicLeapMusicServicePlaybackShuffleState::Unknown:
		return MLMusicServiceShuffleState_Unknown;
	default:
		return MLMusicServiceShuffleState_Unknown;
	};
}

EMagicLeapMusicServicePlaybackShuffleState MLToUnrealShuffleState(const MLMusicServiceShuffleState& InState)
{
	switch (InState)
	{
	case MLMusicServiceShuffleState_Off:
		return EMagicLeapMusicServicePlaybackShuffleState::Off;
	case MLMusicServiceShuffleState_On:
		return EMagicLeapMusicServicePlaybackShuffleState::On;
	case MLMusicServiceShuffleState_Unknown:
		return EMagicLeapMusicServicePlaybackShuffleState::Unknown;
	default:
		return EMagicLeapMusicServicePlaybackShuffleState::Unknown;
	};
}

MLMusicServiceRepeatState UnrealToMLRepeatState(const EMagicLeapMusicServicePlaybackRepeatState& InState)
{
	switch (InState)
	{
	case EMagicLeapMusicServicePlaybackRepeatState::Off:
		return MLMusicServiceRepeatState_Off;
	case EMagicLeapMusicServicePlaybackRepeatState::Song:
		return MLMusicServiceRepeatState_Song;
	case EMagicLeapMusicServicePlaybackRepeatState::Album:
		return MLMusicServiceRepeatState_Album;
	case EMagicLeapMusicServicePlaybackRepeatState::Unkown:
		return MLMusicServiceRepeatState_Unknown;
	default:
		return MLMusicServiceRepeatState_Unknown;
	};
}

EMagicLeapMusicServicePlaybackRepeatState MLToUnrealRepeatState(const MLMusicServiceRepeatState& InState)
{
	switch (InState)
	{
    case MLMusicServiceRepeatState_Off:
		return EMagicLeapMusicServicePlaybackRepeatState::Off;
    case MLMusicServiceRepeatState_Song:
		return EMagicLeapMusicServicePlaybackRepeatState::Song;
    case MLMusicServiceRepeatState_Album:
		return EMagicLeapMusicServicePlaybackRepeatState::Album;
    case MLMusicServiceRepeatState_Unknown:
		return EMagicLeapMusicServicePlaybackRepeatState::Unkown;
	default:
		return EMagicLeapMusicServicePlaybackRepeatState::Unkown;
	};
}

EMagicLeapMusicServiceStatus MLToUnrealServiceStatus(const MLMusicServiceStatus& InStatus)
{
    switch (InStatus)
    {
    case MLMusicServiceStatus_ContextChanged:
        return EMagicLeapMusicServiceStatus::ContextChanged;
    case MLMusicServiceStatus_Created:
        return EMagicLeapMusicServiceStatus::Created;
    case MLMusicServiceStatus_LoggedIn:
        return EMagicLeapMusicServiceStatus::LoggedIn;
    case MLMusicServiceStatus_LoggedOut:
        return EMagicLeapMusicServiceStatus::LoggedOut;
    case MLMusicServiceStatus_NextTrack:
        return EMagicLeapMusicServiceStatus::NextTrack;
    case MLMusicServiceStatus_PrevTrack:
        return EMagicLeapMusicServiceStatus::PreviousTrack;
    case MLMusicServiceStatus_TrackChanged:
        return EMagicLeapMusicServiceStatus::TrackChanged;
    case MLMusicServiceStatus_Unknown:
        return EMagicLeapMusicServiceStatus::Unknown;
	default:
		return EMagicLeapMusicServiceStatus::Unknown;
    };
}
#endif // WITH_MLSDK

bool UMagicLeapMusicServiceFunctionLibrary::Connect(const FString& Name)
{
    bool bSuccess = false;
#if WITH_MLSDK
    MLResult Result = MLMusicServiceConnect(TCHAR_TO_UTF8(*Name));
    bSuccess = (Result == MLResult_Ok);
    UE_CLOG(bSuccess == false, LogMusicServiceFunctionLibrary, Error, TEXT("MLMusicServiceConnect failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
#endif // WITH_MLSDK
    return bSuccess;
}

bool UMagicLeapMusicServiceFunctionLibrary::Disconnect()
{
    bool bSuccess = false;
#if WITH_MLSDK
    MLResult Result = MLMusicServiceDisconnect();
    bSuccess = (Result == MLResult_Ok);
    UE_CLOG(bSuccess == false, LogMusicServiceFunctionLibrary, Error, TEXT("MLMusicServiceDisconnect failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
#endif // WITH_MLSDK
    return bSuccess;
}

bool UMagicLeapMusicServiceFunctionLibrary::SetCallbacks(const FMagicLeapMusicServiceCallbacks& Callbacks)
{
	bool bSuccess = false;
#if WITH_MLSDK

	//Wrap the UE delegates to provide the MLMusicService callback signature. 
	//`data` will contain the UE object. It will be a valid pointer as long as `Disconnect` is called at shutdown.
	MLMusicServiceCallbacks MLCallbacks;
	MLCallbacks.on_playback_state_change = [](MLMusicServicePlaybackState state, void* data)
	{	

		auto UnrealDelegates = static_cast<FMagicLeapMusicServiceCallbacks*>(data);

		AsyncTask(ENamedThreads::GameThread, [=]()
		{
			UnrealDelegates->PlaybackStateDelegate.ExecuteIfBound(MLToUnrealPlaybackState(state));
		});

	};
	MLCallbacks.on_repeat_state_change = [](MLMusicServiceRepeatState state, void* data)
	{

		auto UnrealDelegates = static_cast<FMagicLeapMusicServiceCallbacks*>(data);
		AsyncTask(ENamedThreads::GameThread, [=]()
		{
			UnrealDelegates->RepeatStateDelegate.ExecuteIfBound(MLToUnrealRepeatState(state));
		});

	};
	MLCallbacks.on_shuffle_state_change = [](MLMusicServiceShuffleState state, void* data)
	{

		auto UnrealDelegates = static_cast<FMagicLeapMusicServiceCallbacks*>(data);
		AsyncTask(ENamedThreads::GameThread, [=]()
		{
			UnrealDelegates->ShuffleStateDelegate.ExecuteIfBound(MLToUnrealShuffleState(state));
		});

	};
	MLCallbacks.on_metadata_change = [](const MLMusicServiceMetadata* metadata, void* data)
	{

		auto UnrealDelegates = static_cast<FMagicLeapMusicServiceCallbacks*>(data);
		FMagicLeapMusicServiceTrackMetadata UEMetadata = MLToUnrealServiceMetadata(*metadata);
		AsyncTask(ENamedThreads::GameThread, [=]()
		{
			UnrealDelegates->MetadataDelegate.ExecuteIfBound(UEMetadata);
		});

	};
	MLCallbacks.on_position_change = [](int32_t position, void* data)
	{

		auto UnrealDelegates = static_cast<FMagicLeapMusicServiceCallbacks*>(data);
		AsyncTask(ENamedThreads::GameThread, [=]()
		{
			UnrealDelegates->PositionDelegate.ExecuteIfBound(FTimespan::FromMilliseconds(position));
		});

	};
	MLCallbacks.on_error = [](MLMusicServiceError error_type, int32_t error_code, void* data)
	{

		auto UnrealDelegates = static_cast<FMagicLeapMusicServiceCallbacks*>(data);
		AsyncTask(ENamedThreads::GameThread, [=]()
		{
			UnrealDelegates->ErrorDelegate.ExecuteIfBound(MLToUnrealServiceErrorType(error_type), error_code);
		});

	};
	MLCallbacks.on_status_change = [](MLMusicServiceStatus status, void* data)
	{

		auto UnrealDelegates = static_cast<FMagicLeapMusicServiceCallbacks*>(data);
		AsyncTask(ENamedThreads::GameThread, [=]()
		{
			UnrealDelegates->StatusDelegate.ExecuteIfBound(MLToUnrealServiceStatus(status));
		});

	};
	MLCallbacks.on_volume_change = [](float volume, void* data)
	{

		auto UnrealDelegates = static_cast<FMagicLeapMusicServiceCallbacks*>(data);
		AsyncTask(ENamedThreads::GameThread, [=]()
		{
			UnrealDelegates->VolumeDelegate.ExecuteIfBound(volume);
		});

	};

	//Store the callback state
	FMagicLeapMusicServiceCallbacks& UECallbacks = GetMagicLeapMusicServiceModule().GetDelegates();
	UECallbacks = Callbacks;

	MLResult Result = MLMusicServiceSetCallbacks(&MLCallbacks, &UECallbacks);
	bSuccess = (Result == MLResult_Ok);
	UE_CLOG(bSuccess == false, LogMusicServiceFunctionLibrary, Error, TEXT("MLMusicServiceSetCallbacks failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
#endif // WITH_MLSDK
	return bSuccess;
}

bool UMagicLeapMusicServiceFunctionLibrary::SetAuthenticationString(const FString& AuthenticationString)
{
    bool bSuccess = false;
#if WITH_MLSDK
    MLResult Result = MLMusicServiceSetAuthString(TCHAR_TO_UTF8(*AuthenticationString));
    bSuccess = (Result == MLResult_Ok);
    UE_CLOG(bSuccess == false, LogMusicServiceFunctionLibrary, Error, TEXT("MLMusicServiceSetAuthString failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
#endif // WITH_MLSDK
    return bSuccess;
}

bool UMagicLeapMusicServiceFunctionLibrary::SetPlaybackURL(const FString& PlaybackURL)
{
    bool bSuccess = false;
#if WITH_MLSDK
    MLResult Result = MLMusicServiceSetURL(TCHAR_TO_UTF8(*PlaybackURL));
    bSuccess = (Result == MLResult_Ok);
    UE_CLOG(bSuccess == false, LogMusicServiceFunctionLibrary, Error, TEXT("MLMusicServiceSetURL failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
#endif // WITH_MLSDK
    return bSuccess;
}

bool UMagicLeapMusicServiceFunctionLibrary::SetPlaylist(const TArray<FString>& Playlist)
{
	bool bSuccess = false;
#if WITH_MLSDK
	if (Playlist.Num() == 0)
	{
		return bSuccess;
	}

	uint64 Length = Playlist.Num();
	TArray<const char*> InPlaylist;
	InPlaylist.Init(nullptr, Length);
	for (int32 Index = 0; Index < Length; ++Index)
	{
		InPlaylist[Index] = static_cast<const char*>(FMemory::Malloc(sizeof(char) * Playlist[Index].Len() + 1));
		FCStringAnsi::Strcpy(reinterpret_cast<ANSICHAR*>(const_cast<char*>(InPlaylist[Index])), Playlist[Index].Len(), TCHAR_TO_UTF8(*Playlist[Index]));
	}
	MLResult Result = MLMusicServiceSetPlayList(InPlaylist.GetData(), Length);
	bSuccess = (Result == MLResult_Ok);
	UE_CLOG(bSuccess == false, LogMusicServiceFunctionLibrary, Error, TEXT("MLMusicServiceSetPlayList failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));

	for (int32 Index = 0; Index < Length; ++Index)
	{
		FMemory::Free((void*)(InPlaylist[Index]));
	}

#endif // WITH_MLSDK
	return bSuccess;
}

bool UMagicLeapMusicServiceFunctionLibrary::StartPlayback()
{
    bool bSuccess = false;
#if WITH_MLSDK
    MLResult Result = MLMusicServiceStart();
    bSuccess = (Result == MLResult_Ok);
    UE_CLOG(bSuccess == false, LogMusicServiceFunctionLibrary, Error, TEXT("MLMusicServiceStart failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
#endif // WITH_MLSDK
    return bSuccess;
}

bool UMagicLeapMusicServiceFunctionLibrary::StopPlayback()
{
    bool bSuccess = false;
#if WITH_MLSDK
    MLResult Result = MLMusicServiceStop();
    bSuccess = (Result == MLResult_Ok);
    UE_CLOG(bSuccess == false, LogMusicServiceFunctionLibrary, Error, TEXT("MLMusicServiceStop failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
#endif // WITH_MLSDK
    return bSuccess;
}

bool UMagicLeapMusicServiceFunctionLibrary::PausePlayback()
{
    bool bSuccess = false;
#if WITH_MLSDK
    MLResult Result = MLMusicServicePause();
    bSuccess = (Result == MLResult_Ok);
    UE_CLOG(bSuccess == false, LogMusicServiceFunctionLibrary, Error, TEXT("MLMusicServicePause failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
#endif // WITH_MLSDK
    return bSuccess;
}

bool UMagicLeapMusicServiceFunctionLibrary::ResumePlayback()
{
    bool bSuccess = false;
#if WITH_MLSDK
    MLResult Result = MLMusicServiceResume();
    bSuccess = (Result == MLResult_Ok);
    UE_CLOG(bSuccess == false, LogMusicServiceFunctionLibrary, Error, TEXT("MLMusicServiceResume failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
#endif // WITH_MLSDK
    return bSuccess;
}

bool UMagicLeapMusicServiceFunctionLibrary::SeekTo(const FTimespan& Position)
{
    bool bSuccess = false;
#if WITH_MLSDK
    if (ensure(Position.GetTotalMilliseconds() < UINT32_MAX))
    {
		MLResult Result = MLMusicServiceSeek(static_cast<uint32>(Position.GetTotalMilliseconds()));
		bSuccess = (Result == MLResult_Ok);
		UE_CLOG(bSuccess == false, LogMusicServiceFunctionLibrary, Error, TEXT("MLMusicServiceSeek failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
    }
#endif // WITH_MLSDK
    return bSuccess;
}

bool UMagicLeapMusicServiceFunctionLibrary::NextTrack()
{
    bool bSuccess = false;
#if WITH_MLSDK
    MLResult Result = MLMusicServiceNext();
    bSuccess = (Result == MLResult_Ok);
    UE_CLOG(bSuccess == false, LogMusicServiceFunctionLibrary, Error, TEXT("MLMusicServiceNext failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
#endif // WITH_MLSDK
    return bSuccess;
}

bool UMagicLeapMusicServiceFunctionLibrary::PreviousTrack()
{
    bool bSuccess = false;
#if WITH_MLSDK
    MLResult Result = MLMusicServicePrevious();
    bSuccess = (Result == MLResult_Ok);
    UE_CLOG(bSuccess == false, LogMusicServiceFunctionLibrary, Error, TEXT("MLMusicServicePrevious failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
#endif // WITH_MLSDK
    return bSuccess;
}

bool UMagicLeapMusicServiceFunctionLibrary::GetServiceProviderError(EMagicLeapMusicServiceProviderError& ErrorType, int32& ErrorCode)
{
	bool bSuccess = false;
#if WITH_MLSDK
	MLMusicServiceError InError;
	MLResult Result = MLMusicServiceGetError(&InError, &ErrorCode);
	ErrorType = MLToUnrealServiceErrorType(InError);
	bSuccess = (Result == MLResult_Ok);
	UE_CLOG(bSuccess == false, LogMusicServiceFunctionLibrary, Error, TEXT("MLMusicServiceGetError failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
#endif // WITH_MLSDK
	return bSuccess;
}

bool UMagicLeapMusicServiceFunctionLibrary::GetPlaybackState(EMagicLeapMusicServicePlaybackState& PlaybackState)
{
	bool bSuccess = false;
#if WITH_MLSDK
	MLMusicServicePlaybackState InPlaybackState;
	MLResult Result = MLMusicServiceGetPlaybackState(&InPlaybackState);
	PlaybackState = MLToUnrealPlaybackState(InPlaybackState);
	bSuccess = (Result == MLResult_Ok);
	UE_CLOG(bSuccess == false, LogMusicServiceFunctionLibrary, Error, TEXT("MLMusicServiceGetPlaybackState failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
#endif // WITH_MLSDK
	return bSuccess;
}

bool UMagicLeapMusicServiceFunctionLibrary::SetPlaybackShuffleState(const EMagicLeapMusicServicePlaybackShuffleState& ShuffleState)
{
    bool bSuccess = false;
#if WITH_MLSDK
    MLResult Result = MLMusicServiceSetShuffle(UnrealToMLShuffleState(ShuffleState));
    bSuccess = (Result == MLResult_Ok);
    UE_CLOG(bSuccess == false, LogMusicServiceFunctionLibrary, Error, TEXT("MLMusicServiceSetShuffle failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
#endif // WITH_MLSDK
    return bSuccess;
}

bool UMagicLeapMusicServiceFunctionLibrary::GetPlaybackShuffleState(EMagicLeapMusicServicePlaybackShuffleState& ShuffleState)
{
    bool bSuccess = false;
#if WITH_MLSDK
    MLMusicServiceShuffleState OutShuffleState;
    MLResult Result = MLMusicServiceGetShuffleState(&OutShuffleState);
    ShuffleState = MLToUnrealShuffleState(OutShuffleState);
    bSuccess = (Result == MLResult_Ok);
    UE_CLOG(bSuccess == false, LogMusicServiceFunctionLibrary, Error, TEXT("MLMusicServiceGetShuffle failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
#endif // WITH_MLSDK
    return bSuccess;
}

bool UMagicLeapMusicServiceFunctionLibrary::SetPlaybackRepeatState(const EMagicLeapMusicServicePlaybackRepeatState& RepeatState)
{
    bool bSuccess = false;
#if WITH_MLSDK
    MLResult Result = MLMusicServiceSetRepeat(UnrealToMLRepeatState(RepeatState));
    bSuccess = (Result == MLResult_Ok);
    UE_CLOG(bSuccess == false, LogMusicServiceFunctionLibrary, Error, TEXT("MLMusicServiceSetRepeat failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
#endif // WITH_MLSDK
    return bSuccess;
}

bool UMagicLeapMusicServiceFunctionLibrary::GetPlaybackRepeatState(EMagicLeapMusicServicePlaybackRepeatState& RepeatState)
{
    bool bSuccess = false;
#if WITH_MLSDK
    MLMusicServiceRepeatState OutRepeatState;
    MLResult Result = MLMusicServiceGetRepeatState(&OutRepeatState);
    RepeatState = MLToUnrealRepeatState(OutRepeatState);
    bSuccess = (Result == MLResult_Ok);
    UE_CLOG(bSuccess == false, LogMusicServiceFunctionLibrary, Error, TEXT("MLMusicServiceGetRepeat failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
#endif // WITH_MLSDK
    return bSuccess;
}

bool UMagicLeapMusicServiceFunctionLibrary::SetPlaybackVolume(const float Volume)
{
    bool bSuccess = false;
#if WITH_MLSDK
    MLResult Result = MLMusicServiceSetVolume(FMath::Clamp<float>(Volume, 0.0f, 1.0f));
    bSuccess = (Result == MLResult_Ok);
    UE_CLOG(bSuccess == false, LogMusicServiceFunctionLibrary, Error, TEXT("MLMusicServiceSetVolume failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
#endif // WITH_MLSDK
    return bSuccess;
}

bool UMagicLeapMusicServiceFunctionLibrary::GetPlaybackVolume(float& Volume)
{
    bool bSuccess = false;
#if WITH_MLSDK
    MLResult Result = MLMusicServiceGetVolume(&Volume);
    bSuccess = (Result == MLResult_Ok);
    UE_CLOG(bSuccess == false, LogMusicServiceFunctionLibrary, Error, TEXT("MLMusicServiceGetVolume failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
#endif // WITH_MLSDK
    return bSuccess;
}

bool UMagicLeapMusicServiceFunctionLibrary::GetTrackLength(FTimespan& Length)
{
    bool bSuccess = false;
#if WITH_MLSDK
    uint32 OutLength;
    MLResult Result = MLMusicServiceGetTrackLength(&OutLength);
    bSuccess = (Result == MLResult_Ok);
    if (bSuccess == true)
    {
        Length = FTimespan::FromSeconds(OutLength);
    }
    else
    {
        UE_LOG(LogMusicServiceFunctionLibrary, Error, TEXT("MLMusicServiceGetTrackLength failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
    }
#endif // WITH_MLSDK
    return bSuccess;
}

bool UMagicLeapMusicServiceFunctionLibrary::GetCurrentPosition(FTimespan& Position)
{
    bool bSuccess = false;
#if WITH_MLSDK
    uint32 OutPosition;
    MLResult Result = MLMusicServiceGetCurrentPosition(&OutPosition);
    bSuccess = (Result == MLResult_Ok);
    if (bSuccess == true)
    {
        Position = FTimespan::FromMilliseconds(OutPosition);
    }
    else
    {
        UE_LOG(LogMusicServiceFunctionLibrary, Error, TEXT("MLMusicServiceGetCurrentPosition failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
    }
#endif // WITH_MLSDK
    return bSuccess;
}

bool UMagicLeapMusicServiceFunctionLibrary::GetServiceStatus(EMagicLeapMusicServiceStatus& Status)
{
    bool bSuccess = false;
#if WITH_MLSDK
    MLMusicServiceStatus OutStatus;
    MLResult Result = MLMusicServiceGetStatus(&OutStatus);
    bSuccess = (Result == MLResult_Ok);
    if (bSuccess == true)
    {
		Status = MLToUnrealServiceStatus(OutStatus);
    }
    else
    {
        UE_LOG(LogMusicServiceFunctionLibrary, Error, TEXT("MLMusicServiceGetServiceStatus failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
    }
#endif // WITH_MLSDK
    return bSuccess;
}

bool UMagicLeapMusicServiceFunctionLibrary::GetCurrentTrackMetadata(FMagicLeapMusicServiceTrackMetadata& Metadata)
{
    bool bSuccess = false;
#if WITH_MLSDK
    MLMusicServiceMetadata OutMetadata;
    MLResult Result = MLMusicServiceGetMetadataForIndex(0, &OutMetadata);
    bSuccess = (Result == MLResult_Ok);
    if (bSuccess)
    {
		Metadata = MLToUnrealServiceMetadata(OutMetadata);
		MLMusicServiceReleaseMetadata(&OutMetadata);
    }
    else
    {
        UE_LOG(LogMusicServiceFunctionLibrary, Error, TEXT("MLMusicServiceGetMetadataForIndex failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
    }
#endif // WITH_MLSDK
    return bSuccess;
}

