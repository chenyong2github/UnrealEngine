// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "DirectLink/DirectLinkCommon.h"


class FMessageEndpoint;
struct FMessageAddress;
struct FDirectLinkMsg_DeltaMessage;

namespace DirectLink
{
class ISceneReceiver;
class FScenePipeFromNetwork;


class DATASMITHCORE_API FStreamReceiver
{
public:
	FStreamReceiver(
		TSharedPtr<FMessageEndpoint, ESPMode::ThreadSafe> ThisEndpoint,
		const FMessageAddress& DestinationAddress,
		FStreamPort ReceiverStreamPort,
		const TSharedRef<ISceneReceiver>& Consumer);

	void HandleDeltaMessage(const FDirectLinkMsg_DeltaMessage& Message);

private:
	TSharedPtr<FScenePipeFromNetwork> PipeFromNetwork;
};


} // namespace DirectLink
