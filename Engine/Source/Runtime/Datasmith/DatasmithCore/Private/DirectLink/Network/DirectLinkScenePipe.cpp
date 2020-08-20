// Copyright Epic Games, Inc. All Rights Reserved.

#include "DirectLink/Network/DirectLinkScenePipe.h"

#include "DirectLink/DeltaConsumer.h"
#include "DirectLink/DirectLinkLog.h"
#include "DirectLink/ElementSnapshot.h"
#include "DirectLink/Network/DirectLinkMessages.h"

#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace DirectLink
{


void Serial(FMemoryArchive& Ar, IDeltaConsumer::FOpenDeltaArg& OpenDeltaArg)
{
	Ar << OpenDeltaArg.SceneId.SceneGuid;
	Ar << OpenDeltaArg.SceneId.DisplayName;
	Ar << OpenDeltaArg.ElementCountHint;
}

void Serial(FMemoryArchive& Ar, IDeltaConsumer::FCloseDeltaArg& CloseDeltaArg)
{
}



void FScenePipeToNetwork::SetDeltaProducer(IDeltaProducer* Producer)
{
	DeltaProducer = Producer;
}

void FScenePipeToNetwork::OnOpenDelta(FOpenDeltaArg& OpenDeltaArg)
{
	++BatchNumber;
	if (BatchNumber == 0) // skip 0, which would be considered invalid
	{
		++BatchNumber;
	}

	NextMessageNumber = 0;

	FDirectLinkMsg_DeltaMessage* Message = NewMessage<FDirectLinkMsg_DeltaMessage>(
		FDirectLinkMsg_DeltaMessage::OpenDelta,
		RemoteStreamPort, BatchNumber, NextMessageNumber++
	);

	FMemoryWriter Ar(Message->Payload);
	Serial(Ar, OpenDeltaArg);

	FRWScopeLock _(Lock, SLT_ReadOnly);
	Endpoint->Send(Message, ReceiverAddress);
}

void FScenePipeToNetwork::OnSetElement(FSetElementArg& SetElementArg)
{
	FDirectLinkMsg_DeltaMessage* Message = NewMessage<FDirectLinkMsg_DeltaMessage>(
		FDirectLinkMsg_DeltaMessage::SetElement,
		RemoteStreamPort, BatchNumber, NextMessageNumber++
	);

	FMemoryWriter Ar(Message->Payload);
	SetElementArg.Snapshot.Serialize(Ar);

	FRWScopeLock _(Lock, SLT_ReadOnly);
	Endpoint->Send(Message, ReceiverAddress);
}

void FScenePipeToNetwork::OnCloseDelta(FCloseDeltaArg& CloseDeltaArg)
{
	FDirectLinkMsg_DeltaMessage* Message = NewMessage<FDirectLinkMsg_DeltaMessage>(
		FDirectLinkMsg_DeltaMessage::CloseDelta,
		RemoteStreamPort, BatchNumber, NextMessageNumber++
	);

	FMemoryWriter Ar(Message->Payload);
	Serial(Ar, CloseDeltaArg);

	FRWScopeLock _(Lock, SLT_ReadOnly);
	Endpoint->Send(Message, ReceiverAddress);
}

void FScenePipeFromNetwork::HandleDeltaMessage(const FDirectLinkMsg_DeltaMessage& Message)
{
	UE_LOG(LogDirectLinkNet, Display, TEXT("Delta message received: b:%d m:%d k:%d"), Message.BatchCode, Message.MessageCode, Message.Kind);
	if (CurrentBatchCode == 0)
	{
		CurrentBatchCode = Message.BatchCode;
		NextTransmitableMessageIndex = 0;
	}

	if (Message.BatchCode != CurrentBatchCode)
	{
		return;
	}

	// consume as much as possible
	if (Message.MessageCode == NextTransmitableMessageIndex)
	{
		DelegateDeltaMessage(Message);
		++NextTransmitableMessageIndex;

		FDirectLinkMsg_DeltaMessage XMessage;
		while (MessageBuffer.RemoveAndCopyValue(NextTransmitableMessageIndex, XMessage))
		{
			DelegateDeltaMessage(XMessage);
			++NextTransmitableMessageIndex;
		}
	}
	else
	{
		MessageBuffer.Add(Message.MessageCode, MoveTemp(const_cast<FDirectLinkMsg_DeltaMessage&>(Message)));
	}
}

void FScenePipeFromNetwork::DelegateDeltaMessage(const FDirectLinkMsg_DeltaMessage& Message)
{
	UE_LOG(LogDirectLinkNet, Verbose, TEXT("Delta message transmited: b:%d m:%d k:%d"), Message.BatchCode, Message.MessageCode, Message.Kind);

	check(Consumer);
	switch (Message.Kind)
	{
		case FDirectLinkMsg_DeltaMessage::SetElement:
		{
			IDeltaConsumer::FSetElementArg Arg;
			FMemoryReader Ar(Message.Payload);
			ESerializationStatus SerialResult = Arg.Snapshot.Serialize(Ar);
			switch (SerialResult)
			{
				case ESerializationStatus::Ok:
					Consumer->OnSetElement(Arg);
					break;
				case ESerializationStatus::StreamError:
					UE_LOG(LogDirectLinkNet, Error, TEXT("Delta message issue: Stream Error"));
					break;
				case ESerializationStatus::VersionMinNotRespected:
					UE_LOG(LogDirectLinkNet, Warning, TEXT("Delta message issue: received message version no longer supported"));
					break;
				case ESerializationStatus::VersionMaxNotRespected:
					UE_LOG(LogDirectLinkNet, Warning, TEXT("Delta message issue: received message version unknown"));
					break;
				default:
					ensure(false);
			}
			break;
		}

		case FDirectLinkMsg_DeltaMessage::OpenDelta:
		{
			IDeltaConsumer::FOpenDeltaArg OpenDeltaArg;
			FMemoryReader Ar(Message.Payload);
			Serial(Ar, OpenDeltaArg);
			Consumer->OnOpenDelta(OpenDeltaArg);
			break;
		}

		case FDirectLinkMsg_DeltaMessage::CloseDelta:
		{
			IDeltaConsumer::FCloseDeltaArg CloseDeltaArg;
			FMemoryReader Ar(Message.Payload);
			Serial(Ar, CloseDeltaArg);
			Consumer->OnCloseDelta(CloseDeltaArg);
			CurrentBatchCode = 0;
			break;
		}

		case FDirectLinkMsg_DeltaMessage::None:
		default:
			ensure(false);
	}
}

} // namespace DirectLink
