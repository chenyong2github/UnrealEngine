// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DirectLink/DeltaConsumer.h"
#include "DirectLink/Network/DirectLinkMessages.h"

#include "CoreTypes.h"
#include "IMessageContext.h"

class FMessageEndpoint;
struct FDirectLinkMsg_HaveListMessage;
struct FMessageAddress;

namespace DirectLink
{
class FEndpoint;

/**
 * Responsibility: delegate DeltaConsumer/DeltaProducer link over network, including message ordering and acknowledgments.
 */
class FScenePipeToNetwork
	: public IDeltaConsumer
{
public:
	FScenePipeToNetwork(TSharedPtr<FMessageEndpoint, ESPMode::ThreadSafe> ThisEndpoint, const FMessageAddress& RemoteAddress, FStreamPort RemoteStreamPort)
		: ThisEndpoint(ThisEndpoint)
		, RemoteAddress(RemoteAddress)
		, RemoteStreamPort(RemoteStreamPort)
	{
		check(ThisEndpoint);
	}

	virtual void SetDeltaProducer(IDeltaProducer* Producer) override;
	virtual void SetupScene(FSetupSceneArg& SetupSceneArg) override;
	virtual void OpenDelta(FOpenDeltaArg& OpenDeltaArg) override;
	virtual void OnSetElement(FSetElementArg& SetElementArg) override;
	virtual void RemoveElements(FRemoveElementsArg& RemoveElementsArg) override;
	virtual void OnCloseDelta(FCloseDeltaArg& CloseDeltaArg) override;
	void HandleHaveListMessage(const FDirectLinkMsg_HaveListMessage& Message);

private:
	void DelegateHaveListMessage(const FDirectLinkMsg_HaveListMessage& Message);

private:
	// connectivity
	TSharedPtr<FMessageEndpoint, ESPMode::ThreadSafe> ThisEndpoint;
	FMessageAddress RemoteAddress;
	FStreamPort RemoteStreamPort;

	// sent message ordering
	int8 BatchNumber = 0;
	int32 NextMessageNumber;

	// received message ordering
	TMap<int32, FDirectLinkMsg_HaveListMessage> MessageBuffer;
	int32 NextTransmitableMessageIndex = 0;
	int32 CurrentBatchCode = 0;
	IDeltaProducer* DeltaProducer = nullptr;
};


class FScenePipeFromNetwork
	: public IDeltaProducer
{
public:
	FScenePipeFromNetwork(TSharedPtr<FMessageEndpoint, ESPMode::ThreadSafe> Sender, const FMessageAddress& RemoteAddress, FStreamPort RemoteStreamPort, const TSharedRef<IDeltaConsumer> Consumer)
		: ThisEndpoint(Sender)
		, RemoteAddress(RemoteAddress)
		, RemoteStreamPort(RemoteStreamPort)
		, Consumer(Consumer)
	{
		Consumer->SetDeltaProducer(this);
	}

	void HandleDeltaMessage(const FDirectLinkMsg_DeltaMessage& Message);

	// delta producer
	virtual void OnOpenHaveList(const FSceneIdentifier& HaveSceneId, bool bKeepPreviousContent, int32 SyncCycle) override;
	virtual void OnHaveElement(FSceneGraphId NodeId, FElementHash HaveHash) override;
	void SendHaveElements();
	virtual void OnCloseHaveList() override;

private:
	// transmits messages to the actual delta consumer, reordered
	void DelegateDeltaMessage(const FDirectLinkMsg_DeltaMessage& Message);

private:
	// connectivity
	TSharedPtr<FMessageEndpoint, ESPMode::ThreadSafe> ThisEndpoint;
	FMessageAddress RemoteAddress;
	FStreamPort RemoteStreamPort;

	// sent message ordering
	int32 BatchNumber = 0;
	int32 NextMessageNumber;
	FDirectLinkMsg_HaveListMessage* BufferedHaveListContent = nullptr;
	static constexpr int32 BufferSize = 100;

	// received message ordering
	TMap<int32, FDirectLinkMsg_DeltaMessage> MessageBuffer;
	int32 NextTransmitableMessageIndex = 0;
	int32 CurrentBatchCode = 0;

	TSharedPtr<IDeltaConsumer> Consumer;
};


} // namespace DirectLink
