// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"
#include "Player/PlaylistReader.h"
#include "Player/PlayerSessionServices.h"


namespace Electra
{

struct FMPDLoadRequestDASH;



class IPlaylistReaderDASH : public IPlaylistReader
{
public:
	static TSharedPtrTS<IPlaylistReader> Create(IPlayerSessionServices* PlayerSessionServices);

	virtual ~IPlaylistReaderDASH() = default;

	static const FString OptionKeyMPDLoadConnectTimeout;		//!< (FTimeValue) value specifying connection timeout fetching the MPD
	static const FString OptionKeyMPDLoadNoDataTimeout;			//!< (FTimeValue) value specifying no-data timeout fetching the MPD
	static const FString OptionKeyMPDReloadConnectTimeout;		//!< (FTimeValue) value specifying connection timeout fetching the MPD repeatedly
	static const FString OptionKeyMPDReloadNoDataTimeout;		//!< (FTimeValue) value specifying no-data timeout fetching the MPD repeatedly

	/**
	 * Loads and parses the MPD.
	 *
	 * @param URL     URL of the MPD to load
	 * @param Preferences
	 *                User preferences for initial stream selection.
	 * @param Options Options
	 */
	virtual void LoadAndParse(const FString& URL, const FStreamPreferences& Preferences, const FParamDict& Options) = 0;

	/**
	 * Returns the URL from which the MPD was loaded (or supposed to be loaded).
	 *
	 * @return The MPD URL
	 */
	virtual FString GetURL() const = 0;


	/**
	 * Requests loading of an updated MPD (or an XLINK item)
	 *
	 * @param LoadRequest
	 */
	virtual void RequestMPDUpdate(const FMPDLoadRequestDASH& LoadRequest) = 0;

	/**
	 * Adds load requests for additional elements.
	 */
	virtual void AddElementLoadRequests(const TArray<TWeakPtrTS<FMPDLoadRequestDASH>>& RemoteElementLoadRequests) = 0;

};


} // namespace Electra

