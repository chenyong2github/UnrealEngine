// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "DirectLinkCommon.h"
#include "DirectLinkScenePipe.h"
#include "DirectLinkStreamCommunicationInterface.h"


class FMessageEndpoint;
struct FMessageAddress;
struct FDirectLinkMsg_DeltaMessage;

namespace DirectLink
{
class ISceneReceiver;


class DIRECTLINK_API FStreamReceiver : public IStreamCommunicationInterface
{
public:
	FStreamReceiver(
		TSharedPtr<FMessageEndpoint, ESPMode::ThreadSafe> ThisEndpoint,
		const FMessageAddress& DestinationAddress,
		FStreamPort ReceiverStreamPort,
		const TSharedRef<ISceneReceiver>& Consumer);

	void HandleDeltaMessage(FDirectLinkMsg_DeltaMessage& Message);

	virtual FCommunicationStatus GetCommunicationStatus() const override;

private:
	FScenePipeFromNetwork PipeFromNetwork;
};


} // namespace DirectLink
