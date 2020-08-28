// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Engine.h"
#include "Containers/Array.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MagicLeapMusicServiceTypes.h"
#include "MagicLeapMusicServiceFunctionLibrary.generated.h"

/**
   Provides music playback control of a Music Service Provider
 */
UCLASS(ClassGroup = MagicLeap)
class MAGICLEAPMUSICSERVICE_API UMagicLeapMusicServiceFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
		Connects to a music service provider

		@param Name  Name of the music service provider to connect to.
		@return True if successfully connected to the music provider with name 'Name', false otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "Music Service Function Library|MagicLeap")
	static bool Connect(const FString& Name);

	/**
		Disconnects from the currently connected service provider.

		@return True if successfully disconnected to the currently connected music provider, false otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "Music Service Function Library|MagicLeap")
	static bool Disconnect();

	/**
		Sets callbacks the music service provider will utilize.

		@return True if successful in setting the music service callbacks, false otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "Music Service Function Library|MagicLeap")
	static bool SetCallbacks(const FMagicLeapMusicServiceCallbacks& Callbacks);

	/**
		Sets the authentication string used to connect to the music service provider

		@param AuthenticationString  String used to connect to the music service provider
		@return True if successfully set the authentication string, false otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "Music Service Function Library|MagicLeap")
	static bool SetAuthenticationString(const FString& AuthenticationString);

	/**
		Sets the URL to playback using the music service provider

		@param PlaybackURL  URL to play via the music service provider
		@return True if successfully set the playback URL, false otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "Music Service Function Library|MagicLeap")
	static bool SetPlaybackURL(const FString& PlaybackURL);

	/**
		Sets the playlist for the music service provider

		@param Playlist  Array of track names to be played by the music service provider
		@return True if successfully set the playlist
	 */
	UFUNCTION(BlueprintCallable, Category = "Music Service Function Library|MagicLeap")
	static bool SetPlaylist(const TArray<FString>& Playlist);

	/**
		Starts playback of the currently selected track.

		@return True if playback started successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Music Service Function Library|MagicLeap")
	static bool StartPlayback();

	/**
		Stops playback of the currently selected track.

		@return True if playback stopped successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Music Service Function Library|MagicLeap")
	static bool StopPlayback();

	/**
		Pauses playback of the currently selected track.

		@return True if playback paused successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Music Service Function Library|MagicLeap")
	static bool PausePlayback();

	/**
		Resumes playback of the currently selected track.

		@return True if playback resumed successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Music Service Function Library|MagicLeap")
	static bool ResumePlayback();

	/**
		Seeks to a position on the currently selected track.

		@param Position Position on the current track to seek to.
		@return True if the seeking 'Position' succeeded
	 */
	UFUNCTION(BlueprintCallable, Category = "Music Service Function Library|MagicLeap")
	static bool SeekTo(const FTimespan& Position);

	/**
		Advances to the next track on the music service provider's playlist.

		@return True if advancing to the next track succeded
	 */
	UFUNCTION(BlueprintCallable, Category = "Music Service Function Library|MagicLeap")
	static bool NextTrack();

	/**
		Rewinds to the previous track on the music provider's playlist

		@return True if rewind to the previous track succeded
	 */
	UFUNCTION(BlueprintCallable, Category = "Music Service Function Library|MagicLeap")
	static bool PreviousTrack();

	/**
		Gets the current error status of the music service provider.
		If no error has occurred then this will return the error type 'None'

		@return True if retreiving the providers error has succeeded
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Music Service Function Library|MagicLeap")
	static bool GetServiceProviderError(EMagicLeapMusicServiceProviderError& ErrorType, int32& ErrorCode);

	/**
		Gets the current playback state of the music service provider

		@return True if retreiving the playback state succeeded
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Music Service Function Library|MagicLeap")
	static bool GetPlaybackState(EMagicLeapMusicServicePlaybackState& PlaybackState);

	/**
		Sets the current playback shuffle state

		@param ShuffleState  The shuffle type to set the current shuffle state to.
		@return True if setting the shuffle state to 'ShuffleState' succeeds
	 */
	UFUNCTION(BlueprintCallable, Category = "Music Service Function Library|MagicLeap")
	static bool SetPlaybackShuffleState(const EMagicLeapMusicServicePlaybackShuffleState& ShuffleState);

	/**
		Gets the current playback shuffle state

		@param ShuffleState  The current playback shuffle state the music provider is set to.
		@return True if retrieving the shuffle state succeeded
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Music Service Function Library|MagicLeap")
	static bool GetPlaybackShuffleState(EMagicLeapMusicServicePlaybackShuffleState& ShuffleState);

	/**
		Sets the repeat state of the music service provider

		@param RepeatState  Repeat state to set the music service provider to
		@return True if setting the repeat state to 'RepeatState' succeeded
	 */
	UFUNCTION(BlueprintCallable, Category = "Music Service Function Library|MagicLeap")
	static bool SetPlaybackRepeatState(const EMagicLeapMusicServicePlaybackRepeatState& RepeatState);

	/**
		Gets the current repeat state of the music service provider

		@param RepeatState  The repeat state the music service provider is currently set to
		@return True if retrieving the repeat state succeeds
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Music Service Function Library|MagicLeap")
	static bool GetPlaybackRepeatState(EMagicLeapMusicServicePlaybackRepeatState& RepeatState);

	/**
		Sets the volume of playback by the music service provider

		@param Volume  Volume to set playback to, should range between 0.0 and 1.0
		@return True if playback volume has successfully been changed to 'Volume'
	 */
	UFUNCTION(BlueprintCallable, Category = "Music Service Function Library|MagicLeap")
	static bool SetPlaybackVolume(const float Volume);

	/**
		Gets the volume of playback the music service provider is currently set to

		@param Volume  Volume of current playback
		@return True if the playback volume has sucessfully been retrieved
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Music Service Function Library|MagicLeap")
	static bool GetPlaybackVolume(float& Volume);

	/**
		Gets the length of the currently selected track

		@param Length  Length of the currently selected track
		@return True if the length of the current track has been sucessfully retrieved
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Music Service Function Library|MagicLeap")
	static bool GetTrackLength(FTimespan& Length);

	/**
		Gets the current playback position of the currently selected track

		@param Position Position of playback of the current track
		@return True if retrieving the current track position succeeded
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Music Service Function Library|MagicLeap")
	static bool GetCurrentPosition(FTimespan& Position);

	/**
		Gets the current service status of the music service provider

		@param Status Current service status of the music service provider
		@return True if retrieving the service status of the current music service provider succeeded
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Music Service Function Library|MagicLeap")
	static bool GetServiceStatus(EMagicLeapMusicServiceStatus& Status);

	/**
		Gets the currently selected track's metadata

		@param Metadata  Metadata of the currently selected track
		@return True if retrieving the current track metadata succeeded
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Music Service Function Library|MagicLeap")
	static bool GetCurrentTrackMetadata(FMagicLeapMusicServiceTrackMetadata& Metadata);
};
