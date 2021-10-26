// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Modules/ModuleInterface.h"

class FImgMediaGlobalCache;
class IMediaEventSink;
class IMediaPlayer;


/**
 * Interface for the ImgMedia module.
 */
class IImgMediaModule
	: public IModuleInterface
{
public:

	/**
	 * Creates a media player for image sequences.
	 *
	 * @param EventHandler The object that will receive the player's events.
	 * @return A new media player, or nullptr if a player couldn't be created.
	 */
	virtual TSharedPtr<IMediaPlayer, ESPMode::ThreadSafe> CreatePlayer(IMediaEventSink& EventHandler) = 0;

public:

	/** Virtual destructor. */
	virtual ~IImgMediaModule() { }

	/**
	 * Call this to get the global cache.
	 * 
	 * @return Global cache.
	 */
	static FImgMediaGlobalCache* GetGlobalCache() { return GlobalCache.Get(); }

protected:

	/** Holds the global cache. */
	static TSharedPtr<FImgMediaGlobalCache, ESPMode::ThreadSafe> GlobalCache;
};
