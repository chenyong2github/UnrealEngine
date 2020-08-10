// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DirectLink/DeltaConsumer.h"
#include "DirectLink/Network/DirectLinkMessages.h"
#include "DirectLink/Network/DirectLinkEndpoint.h"

#include "CoreTypes.h"
#include "IMessageContext.h"
#include "Misc/Guid.h"


namespace DirectLink
{
class FEndpoint;


class FScenePipeBase
{ // stripped from its content, could be removed I guess
public:
	virtual ~FScenePipeBase() = default;

protected:
	FScenePipeBase(const FSceneIdentifier& SceneId)
		: SceneId(SceneId)
	{}

public:
	const FSceneIdentifier SceneId;
	// #ue_directlink_streams no scene info in pipes... just dest/src
};

/**
 * Responsibility: delegate DeltaConsumer/DeltaProducer link over network, including message ordering and acknowledgments.
 */
class FScenePipeToNetwork
	: public FScenePipeBase
	, public IDeltaConsumer
{
public:
	FScenePipeToNetwork(TSharedPtr<FMessageEndpoint, ESPMode::ThreadSafe> Sender, const FSceneIdentifier& Scene)
		: FScenePipeBase(Scene)
		, Endpoint(Sender)
		, ConnectionEvent(FPlatformProcess::GetSynchEventFromPool(true))
	{
		check(Endpoint);
	}

	~FScenePipeToNetwork()
	{
		FPlatformProcess::ReturnSynchEventToPool(ConnectionEvent);
	}

	bool IsConnected()
	{
		if (Endpoint)
		{
			FRWScopeLock _(Lock, SLT_ReadOnly);
			return ReceiverAddress.IsValid();
		}
		return false;
	}

	bool WaitForConnection(FTimespan Timeout)
	{
		ConnectionEvent->Wait(Timeout);
		return IsConnected();
	}

	void SetDestinationInfo(const FMessageAddress& Dest, FStreamPort ReceiverStreamPort)
	{
		FRWScopeLock _(Lock, SLT_Write);
		ReceiverAddress = Dest;
		RemoteStreamPort = ReceiverStreamPort;
		ConnectionEvent->Trigger();
	}

private:
	virtual void SetDeltaProducer(IDeltaProducer* Producer) override;
	virtual void OnOpenDelta(FOpenDeltaArg& OpenDeltaArg) override;
	virtual void OnSetElement(FSetElementArg& SetElementArg) override;
	virtual void OnCloseDelta(FCloseDeltaArg& CloseDeltaArg) override;

private:
	IDeltaProducer* DeltaProducer;

	// connectivity:
	TSharedPtr<FMessageEndpoint, ESPMode::ThreadSafe> Endpoint;
	class FEvent* ConnectionEvent = nullptr;
	FRWLock Lock;

	// rooting
	FMessageAddress ReceiverAddress;
	FStreamPort RemoteStreamPort = 0;

	// ordering
	int8 BatchNumber = 0;
	int32 NextMessageNumber;
};


class FScenePipeFromNetwork
	: public FScenePipeBase
	, public IDeltaProducer
{
public:
	FScenePipeFromNetwork(const TSharedPtr<IDeltaConsumer> Consumer, const FSceneIdentifier& SceneId)
		: FScenePipeBase(SceneId)
		, Consumer(Consumer)
	{}

	void HandleDeltaMessage(const FDirectLinkMsg_DeltaMessage& Message);

private:
	// transmits messages to the actual delta consumer, reorderred
	void DelegateDeltaMessage(const FDirectLinkMsg_DeltaMessage& Message);

	TSharedPtr<IDeltaConsumer> Consumer;

	TMap<int32, FDirectLinkMsg_DeltaMessage> MessageBuffer;
	int32 NextTransmitableMessageIndex = 0;
	int32 CurrentBatchCode = 0;
};



} // namespace DirectLink
