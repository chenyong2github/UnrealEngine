// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"

#include "MediaPlayerProxyInterface.generated.h"

class UMediaSource; 
struct FMediaSourceCacheSettings;

/**
 * The proxy object provides a higher level of insight/control than just the media player.
 * For example, the object owning the player may control the player in some
 * cases, and the proxy allows you and the object to avoid conflicts in control.
 */
UINTERFACE(MinimalAPI)
class UMediaPlayerProxyInterface : public UInterface
{
	GENERATED_BODY()
};

class MEDIAASSETS_API IMediaPlayerProxyInterface
{
	GENERATED_BODY()

public:
	/**
	 * Get the desired playback rate.
	 * Note that this is not necessarily the actual rate of the player,
	 * merely the desired rate the user wants.
	 */
	virtual float GetProxyRate() const = 0;

	/**
	 * Changes the desired playback rate.
	 *
	 * @param Rate		The playback rate to set.
	 * @return			True on success, false otherwise.
	 */
	virtual bool SetProxyRate(float Rate) = 0;

	/**
	 * Call this to see if you can control the media player, or if the owning object is using it.
	 * 
	 * @return				True if you can control the player.
	 */
	virtual bool IsExternalControlAllowed() = 0;

	/**
	 * Gets the cache settings for the player.
	 */
	virtual const FMediaSourceCacheSettings& GetCacheSettings() const = 0;

	/**
	 * Get the media source for a given index.
	 */
	virtual UMediaSource* GetMediaSourceFromIndex(int32 Index) const = 0;

	/**
	 * Close the player.
	 */
	virtual void ProxyClose() = 0;

	/**
	 * Ask if a specific track in the playlist is playing.
	 */
	virtual bool ProxyIsPlaylistIndexPlaying(int32 Index) const = 0;

	/**
	 * Open a specific track in the playlist.
	 */
	virtual void ProxyOpenPlaylistIndex(int32 Index) = 0;

	/**
	 * Set the player to play on open.
	 */
	virtual void ProxySetPlayOnOpen(bool bInPlayOnOpen) = 0;
	
};
