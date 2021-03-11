// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"
#include "Player/PlaylistReader.h"
#include "Player/PlayerSessionServices.h"


namespace Electra
{

struct FMPDLoadRequestDASH;
class FManifestDASHInternal;



class IPlaylistReaderDASH : public IPlaylistReader
{
public:
	static TSharedPtrTS<IPlaylistReader> Create(IPlayerSessionServices* PlayerSessionServices);

	virtual ~IPlaylistReaderDASH() = default;

	/**
	 * Loads and parses the MPD.
	 *
	 * @param URL     URL of the MPD to load
	 */
	virtual void LoadAndParse(const FString& URL) = 0;

	/**
	 * Returns the URL from which the MPD was loaded (or supposed to be loaded).
	 *
	 * @return The MPD URL
	 */
	virtual FString GetURL() const = 0;

	/**
	 * Adds load requests for additional elements.
	 * 
	 * @param RemoteElementLoadRequests
	 */
	virtual void AddElementLoadRequests(const TArray<TWeakPtrTS<FMPDLoadRequestDASH>>& RemoteElementLoadRequests) = 0;

	/**
	 * Requests an MPD update.
	 */
	virtual void RequestMPDUpdate(bool bForcedUpdate) = 0;

	/**
	 * Access the current internal MPD
	 */
	virtual TSharedPtrTS<FManifestDASHInternal> GetCurrentMPD() = 0;
};


} // namespace Electra

