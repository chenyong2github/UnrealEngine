// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"
#include "Player/PlaybackTimeline.h"
#include "Player/PlaylistReader.h"
#include "Player/PlayerSessionServices.h"
#include "PlaylistReaderDASH_Internal.h"
#include "StreamTypes.h"
#include "ErrorDetail.h"


namespace Electra
{

class IManifestBuilderDASH
{
public:
	static IManifestBuilderDASH* Create(IPlayerSessionServices* PlayerSessionServices);

	virtual ~IManifestBuilderDASH() = default;

	/**
	 * Builds a new internal manifest from a DASH MPD
	 *
	 * @param OutMPD
	 * @param InOutMPDXML
	 * @param Request
	 * @param Preferences
	 * @param Options
	 *
	 * @return
	 */
	virtual FErrorDetail BuildFromMPD(TSharedPtrTS<FManifestDASHInternal>& OutMPD, TCHAR* InOutMPDXML, TSharedPtrTS<FMPDLoadRequestDASH> Request, const FStreamPreferences& Preferences, const FParamDict& Options) = 0;
};


} // namespace Electra




