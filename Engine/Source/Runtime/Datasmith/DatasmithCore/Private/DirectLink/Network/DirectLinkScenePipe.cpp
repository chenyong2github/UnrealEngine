// Copyright Epic Games, Inc. All Rights Reserved.

#include "DirectLink/Network/DirectLinkScenePipe.h"

#include "DirectLink/DirectLinkLog.h"
#include "DirectLink/ElementSnapshot.h"
#include "DirectLink/Network/DirectLinkMessages.h"
#include "DirectLink/SceneSnapshot.h"

#include "MessageEndpoint.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace DirectLink
{

int32 GetDeltaMessageTargetSizeByte()
{
	static int32 i = 64*1024; // #ue_directlink_config
	return i;
}


template<typename MessageType>
void FPipeBase::SendInternal(MessageType* Message, int32 ByteSizeHint)
{
	auto flag = EMessageFlags::Reliable;
	ThisEndpoint->Send(Message, MessageType::StaticStruct(), flag, nullptr, TArrayBuilder<FMessageAddress>().Add(RemoteAddress), FTimespan::Zero(), FDateTime::MaxValue());
}


void FScenePipeToNetwork::SetDeltaProducer(IDeltaProducer* Producer)
{
	check(Producer);
	DeltaProducer = Producer;
}


void FScenePipeToNetwork::SetupScene(FSetupSceneArg& SetupSceneArg)
{
	FDirectLinkMsg_DeltaMessage* Message = NewMessage<FDirectLinkMsg_DeltaMessage>(
		FDirectLinkMsg_DeltaMessage::SetupScene, RemoteStreamPort, 0, 0
	);

	FMemoryWriter Ar(Message->Payload);
	Ar << SetupSceneArg;

	Send(Message);
}


void FScenePipeToNetwork::OpenDelta(FOpenDeltaArg& OpenDeltaArg)
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
	Ar << OpenDeltaArg;

	Send(Message);
}


void FScenePipeToNetwork::Send(FDirectLinkMsg_DeltaMessage* Message)
{
	SendInternal(Message, Message->Payload.Num());
}


void FScenePipeFromNetwork::Send(FDirectLinkMsg_HaveListMessage* Message)
{
	SendInternal(Message, Message->Payload.Num());
}


void FScenePipeToNetwork::InitSetElementBuffer()
{
	SetElementBuffer.Reset(GetDeltaMessageTargetSizeByte());
}


void FScenePipeToNetwork::OnSetElement(FSetElementArg& SetElementArg)
{
	bool bAppend = true;
	FMemoryWriter Ar(SetElementBuffer, false, bAppend);
	ESerializationStatus Status = SetElementArg.Snapshot->Serialize(Ar);
	Ar << SetElementArg.ElementIndexHint;
	check(Status == ESerializationStatus::Ok); // write should never be an issue

	if (SetElementBuffer.Num() >= 0.9 * GetDeltaMessageTargetSizeByte())
	{
		SendSetElementBuffer();
	}
}


void FScenePipeToNetwork::SendSetElementBuffer()
{
	FDirectLinkMsg_DeltaMessage* Message = NewMessage<FDirectLinkMsg_DeltaMessage>(
		FDirectLinkMsg_DeltaMessage::SetElements,
		RemoteStreamPort, BatchNumber, NextMessageNumber++
	);
	// #ue_directlink_optim Investigate FCompression::CompressMemory
	Message->Payload = MoveTemp(SetElementBuffer);
	InitSetElementBuffer();

	Send(Message);
}


void FScenePipeToNetwork::RemoveElements(FRemoveElementsArg& RemoveElementsArg)
{
	FDirectLinkMsg_DeltaMessage* Message = NewMessage<FDirectLinkMsg_DeltaMessage>(
		FDirectLinkMsg_DeltaMessage::RemoveElements,
		RemoteStreamPort, BatchNumber, NextMessageNumber++
	);

	FMemoryWriter Ar(Message->Payload);
	Ar << RemoveElementsArg.Elements;

	Send(Message);
}


void FScenePipeToNetwork::OnCloseDelta(FCloseDeltaArg& CloseDeltaArg)
{
	SendSetElementBuffer();
	FDirectLinkMsg_DeltaMessage* Message = NewMessage<FDirectLinkMsg_DeltaMessage>(
		FDirectLinkMsg_DeltaMessage::CloseDelta,
		RemoteStreamPort, BatchNumber, NextMessageNumber++
	);

	FMemoryWriter Ar(Message->Payload);
	Ar << CloseDeltaArg;

	Send(Message);
}


///////////////////////////////////////////////////////////////////////////////////////////////////


void FScenePipeFromNetwork::HandleDeltaMessage(const FDirectLinkMsg_DeltaMessage& Message)
{
	UE_LOG(LogDirectLinkNet, Verbose, TEXT("Delta message received: b:%d m:%d k:%d"), Message.BatchCode, Message.MessageCode, Message.Kind);
	if (Message.BatchCode == 0)
	{
		DelegateDeltaMessage(Message);
		return;
	}

	if (CurrentBatchCode == 0)
	{
		CurrentBatchCode = Message.BatchCode;
		NextTransmitableMessageIndex = 0;
	}

	if (Message.BatchCode != CurrentBatchCode)
	{
		UE_LOG(LogDirectLinkNet, Warning, TEXT("Dropped delta message (bad batch code %d, expected %d)"), Message.BatchCode, CurrentBatchCode);
		return;
	}

	// consume as much as possible
	if (Message.MessageCode == NextTransmitableMessageIndex)
	{
		DelegateDeltaMessage(Message);
		++NextTransmitableMessageIndex;

		FDirectLinkMsg_DeltaMessage NextMessage;
		while (MessageBuffer.RemoveAndCopyValue(NextTransmitableMessageIndex, NextMessage))
		{
			DelegateDeltaMessage(NextMessage);
			++NextTransmitableMessageIndex;
		}
	}
	else
	{
		MessageBuffer.Add(Message.MessageCode, MoveTemp(const_cast<FDirectLinkMsg_DeltaMessage&>(Message)));
	}
}


void FScenePipeFromNetwork::OnOpenHaveList(const FSceneIdentifier& HaveSceneId, bool bKeepPreviousContent, int32 SyncCycle)
{
	if (!ensure(BufferedHaveListContent == nullptr))
	{
		// we should not be opening a new have list without having the previous one closed
		SendHaveElements();
	}

	BatchNumber = SyncCycle;
	NextMessageNumber = 0;

	FDirectLinkMsg_HaveListMessage* Message = NewMessage<FDirectLinkMsg_HaveListMessage>(
		FDirectLinkMsg_HaveListMessage::EKind::OpenHaveList,
		RemoteStreamPort, BatchNumber, NextMessageNumber++
	);

	FMemoryWriter Ar(Message->Payload);
	check(Ar.IsSaving());
	Ar << const_cast<FSceneIdentifier&>(HaveSceneId);
	Ar << bKeepPreviousContent;

	UE_LOG(LogDirectLinkNet, Verbose, TEXT("Send OpenHave list b:%d m:%d k:%d"), Message->SyncCycle, Message->MessageCode, Message->Kind);

	Send(Message);
}


void FScenePipeFromNetwork::OnHaveElement(FSceneGraphId NodeId, FElementHash HaveHash)
{
	if (BufferedHaveListContent == nullptr)
	{
		BufferedHaveListContent = NewMessage<FDirectLinkMsg_HaveListMessage>(
			FDirectLinkMsg_HaveListMessage::EKind::HaveListElement,
			RemoteStreamPort, 0, 0
		);
		BufferedHaveListContent->NodeIds.Reserve(BufferSize);
		BufferedHaveListContent->Hashes.Reserve(BufferSize);
	}
	BufferedHaveListContent->NodeIds.Add(NodeId);
	BufferedHaveListContent->Hashes.Add(HaveHash);

	if (BufferedHaveListContent->NodeIds.Num() >= BufferSize)
	{
		SendHaveElements();
	}
}


void FScenePipeFromNetwork::SendHaveElements()
{
	if (BufferedHaveListContent)
	{
		BufferedHaveListContent->SyncCycle = BatchNumber;
		BufferedHaveListContent->MessageCode = NextMessageNumber++;

		ThisEndpoint->Send(BufferedHaveListContent, RemoteAddress);
		BufferedHaveListContent = nullptr;
	}
}


void FScenePipeFromNetwork::OnCloseHaveList()
{
	SendHaveElements();

	FDirectLinkMsg_HaveListMessage* Message = NewMessage<FDirectLinkMsg_HaveListMessage>(
		FDirectLinkMsg_HaveListMessage::EKind::CloseHaveList,
		RemoteStreamPort, BatchNumber, NextMessageNumber++
	);

	UE_LOG(LogDirectLinkNet, Verbose, TEXT("Send OpenHave list b:%d m:%d k:%d"), Message->SyncCycle, Message->MessageCode, Message->Kind);
	Send(Message);
}


void FScenePipeFromNetwork::DelegateDeltaMessage(const FDirectLinkMsg_DeltaMessage& Message)
{
	UE_LOG(LogDirectLinkNet, Verbose, TEXT("Delta message transmited: b:%d m:%d k:%d"), Message.BatchCode, Message.MessageCode, Message.Kind);

	// acknowledge message (allows the sender to track communication progress)
	auto* AckMessage = NewMessage<FDirectLinkMsg_HaveListMessage>(
		FDirectLinkMsg_HaveListMessage::EKind::AckDeltaMessage,
		RemoteStreamPort, Message.BatchCode, Message.MessageCode
	);
	Send(AckMessage);

	// process message
	check(Consumer);
	switch (Message.Kind)
	{
		case FDirectLinkMsg_DeltaMessage::SetupScene:
		{
			CurrentCommunicationStatus.bIsReceiving = true;

			IDeltaConsumer::FSetupSceneArg SetupSceneArg;
			FMemoryReader Ar(Message.Payload);
			Ar << SetupSceneArg;
			Consumer->SetupScene(SetupSceneArg);
			break;
		}

		case FDirectLinkMsg_DeltaMessage::OpenDelta:
		{
			IDeltaConsumer::FOpenDeltaArg OpenDeltaArg;
			FMemoryReader Ar(Message.Payload);
			Ar << OpenDeltaArg;
			CurrentCommunicationStatus.TaskTotal = OpenDeltaArg.ElementCountHint + 1; // "+1" in order to reach 100% artificially on the last message
			Consumer->OpenDelta(OpenDeltaArg);
			break;
		}

		case FDirectLinkMsg_DeltaMessage::SetElements:
		{
			FMemoryReader Ar(Message.Payload);
			bool bSerialOk = true;
			while (bSerialOk && Ar.Tell() < Message.Payload.Num())
			{
				IDeltaConsumer::FSetElementArg SetElementArg;
				SetElementArg.Snapshot = MakeShared<FElementSnapshot>();
				ESerializationStatus SerialResult = SetElementArg.Snapshot->Serialize(Ar);
				Ar << SetElementArg.ElementIndexHint;

				bSerialOk = false;
				switch (SerialResult)
				{
					case ESerializationStatus::Ok:
						Consumer->OnSetElement(SetElementArg);
						CurrentCommunicationStatus.TaskCompleted = SetElementArg.ElementIndexHint;
						bSerialOk = true;
						break;
					case ESerializationStatus::StreamError:
						// #ue_directlink_syncprotocol notify sender of unrecoverable errors
						UE_LOG(LogDirectLinkNet, Error, TEXT("Delta message issue: Stream Error"));
						break;
					case ESerializationStatus::VersionMinNotRespected:
						UE_LOG(LogDirectLinkNet, Error, TEXT("Delta message issue: received message version no longer supported"));
						break;
					case ESerializationStatus::VersionMaxNotRespected:
						UE_LOG(LogDirectLinkNet, Error, TEXT("Delta message issue: received message version unknown"));
						break;
					default:
						ensure(false);
				}
			}
			break;
		}

		case FDirectLinkMsg_DeltaMessage::RemoveElements:
		{
			IDeltaConsumer::FRemoveElementsArg RemoveElementsArg;
			FMemoryReader Ar(Message.Payload);
			Ar << RemoveElementsArg.Elements;
			Consumer->RemoveElements(RemoveElementsArg);
			break;
		}

		case FDirectLinkMsg_DeltaMessage::CloseDelta:
		{
			CurrentCommunicationStatus.bIsReceiving = false;
			CurrentCommunicationStatus.TaskCompleted = CurrentCommunicationStatus.TaskTotal;

			IDeltaConsumer::FCloseDeltaArg CloseDeltaArg;
			FMemoryReader Ar(Message.Payload);
			Ar << CloseDeltaArg;
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
