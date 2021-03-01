// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"
#include "Player/PlayerSessionServices.h"
#include "ErrorDetail.h"
#include "MPDElementsDASH.h"


namespace Electra
{

namespace IManifestParserDASH
{
FErrorDetail BuildFromMPD(FDashMPD_RootEntities& OutRootEntities, TArray<TWeakPtrTS<IDashMPDElement>>& OutXLinkElements, TCHAR* InOutMPDXML, const TCHAR* InExpectedRootElement, IPlayerSessionServices* InPlayerSessionServices);
};

} // namespace Electra

