// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DirectLink/DeltaConsumer.h"
#include "DirectLink/DirectLinkCommon.h"

#include "Containers/UnrealString.h"
#include "CoreTypes.h"


namespace DirectLink
{

/**
 * In DirectLink, Source points can request connections on Destinations points.
 * For each destination, an instance of this class is used to accept/refuse incoming connections requests, and provide an associated DeltaConsumer.
 * Each stream (pair Source-Destination) should have a distinct DeltaConsumer
 */
class ISceneProvider
{
public:
	struct FSourceInformation
	{
		FGuid Id;
	};

public:
	virtual ~ISceneProvider() = default;

	/**
	 * @param Source    Information about the incoming Source
	 * @return whether the source can be accepted as input of the Destination
	 */
	virtual bool CanOpenNewConnection(const FSourceInformation& Source) = 0;

	/**
	 * @param Source    Information about the incoming Source
	 * @return DeltaConsumer dedicated for this source that will receive Delta information from the source
	 */
	virtual TSharedPtr<ISceneReceiver> GetSceneReceiver(const FSourceInformation& Source) = 0;
};

} // namespace DirectLink

