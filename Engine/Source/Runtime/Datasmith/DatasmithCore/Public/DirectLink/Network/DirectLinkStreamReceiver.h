// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "DirectLink/DirectLinkCommon.h"
#include "DirectLink/Network/DirectLinkScenePipe.h"
#include "DirectLink/Network/DirectLinkStreamCommunicationInterface.h"


class FMessageEndpoint;
struct FMessageAddress;
struct FDirectLinkMsg_DeltaMessage;

namespace DirectLink
{
class ISceneReceiver;


class DATASMITHCORE_API FStreamReceiver : public IStreamCommunicationInterface
{
public:
	FStreamReceiver(
		TSharedPtr<FMessageEndpoint, ESPMode::ThreadSafe> ThisEndpoint,
		const FMessageAddress& DestinationAddress,
		FStreamPort ReceiverStreamPort,
		const TSharedRef<ISceneReceiver>& Consumer);

	void HandleDeltaMessage(const FDirectLinkMsg_DeltaMessage& Message);

	virtual FCommunicationStatus GetCommunicationStatus() const override;

private:
	FScenePipeFromNetwork PipeFromNetwork;
};


} // namespace DirectLink
