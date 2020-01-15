// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolTransportArtNet.h"
#include "DMXProtocolArtNet.h"

#include "Common/UdpSocketSender.h"
#include "HAL/Event.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "IMessageAttachment.h"
#include "Serialization/ArrayReader.h"
#include "Serialization/ArrayWriter.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

static FTimespan CalculateWaitTime()
{
	return FTimespan::FromMilliseconds(10);
}

FDMXProtocolSenderArtNet::FDMXProtocolSenderArtNet(FSocket& InSocket, FDMXProtocolArtNet* InProtocol)
	: LastSentPackage(-1)
	, bRequestingExit(false)
	, BroadcastSocket(&InSocket)
	, Protocol(InProtocol)
{
	WorkEvent = MakeShareable(FPlatformProcess::GetSynchEventFromPool(), [](FEvent* EventToDelete)
	{
		FPlatformProcess::ReturnSynchEventToPool(EventToDelete);
	});

	Thread = FRunnableThread::Create(this, TEXT("FDMXProtocolSenderArtNet"), 128 * 1024, TPri_BelowNormal, FPlatformAffinity::GetPoolThreadMask());
}

FDMXProtocolSenderArtNet::~FDMXProtocolSenderArtNet()
{
	// Shut Down worker thread
	if (Thread != nullptr)
	{
		Thread->Kill(true);
		delete Thread;
	}
}

bool FDMXProtocolSenderArtNet::Init()
{
	return true;
}

uint32 FDMXProtocolSenderArtNet::Run()
{
	while (StopTaskCounter.GetValue() == 0)
	{
		if (WorkEvent->Wait(CalculateWaitTime()))
		{
			ConsumeOutboundPackages();
		}
	}

	return 0;
}

void FDMXProtocolSenderArtNet::Stop()
{
	StopTaskCounter.Increment();

	if (WorkEvent)
	{
		WorkEvent->Trigger();
		Thread->WaitForCompletion();
	}
}

void FDMXProtocolSenderArtNet::Exit()
{
}

void FDMXProtocolSenderArtNet::Tick()
{
	ConsumeOutboundPackages();
}

FSingleThreadRunnable* FDMXProtocolSenderArtNet::GetSingleThreadInterface()
{
	return this;
}

bool FDMXProtocolSenderArtNet::EnqueueOutboundPackage(FDMXPacketPtr Packet)
{
	if (StopTaskCounter.GetValue())
	{
		return false;
	}

	if (!OutboundPackages.Enqueue(Packet))
	{
		return false;
	}

	WorkEvent->Trigger();


	return true;
}

void FDMXProtocolSenderArtNet::ConsumeOutboundPackages()
{
	FScopeLock ScopeLock(&CriticalSection);

	FDMXPacketPtr Packet;
	while (OutboundPackages.Dequeue(Packet))
	{
		++LastSentPackage;
		int32 BytesSent = 0;
		bool bIsSent = BroadcastSocket->SendTo(Packet->Data.GetData(), Packet->Data.Num(), BytesSent, *Protocol->GetBroadcastAddr());
		if (!bIsSent)
		{
			UE_LOG_DMXPROTOCOL(Error, TEXT("Error sending %d"), bIsSent);
		}
	}
}

FDMXProtocolReceiverArtNet::FDMXProtocolReceiverArtNet(FSocket& InSocket, FDMXProtocolArtNet* InProtocol, const FTimespan& InWaitTime)
	: Socket(&InSocket)
	, Stopping(false)
	, Thread(nullptr)
	, WaitTime(InWaitTime)
{
	check(Socket != nullptr);
	check(Socket->GetSocketType() == SOCKTYPE_Datagram);

	SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);

	Thread = FRunnableThread::Create(this, *ThreadName, 128 * 1024, TPri_Normal, FPlatformAffinity::GetPoolThreadMask());
}

FDMXProtocolReceiverArtNet::~FDMXProtocolReceiverArtNet()
{
	if (Thread != nullptr)
	{
		Thread->Kill(true);
		delete Thread;
	}
}

FOnDMXDataReceived& FDMXProtocolReceiverArtNet::OnDataReceived()
{
	return DMXDataReceiveDelegate;
}

FRunnableThread* FDMXProtocolReceiverArtNet::GetThread() const
{
	return Thread;
}

bool FDMXProtocolReceiverArtNet::Init()
{
	return true;
}

uint32 FDMXProtocolReceiverArtNet::Run()
{
	while (!Stopping)
	{
		Update(WaitTime);
	}

	return 0;
}

void FDMXProtocolReceiverArtNet::Stop()
{
	Stopping = true;
}

void FDMXProtocolReceiverArtNet::Exit()
{
}

void FDMXProtocolReceiverArtNet::Tick()
{
	Update(FTimespan::Zero());
}

FSingleThreadRunnable* FDMXProtocolReceiverArtNet::GetSingleThreadInterface()
{
	return this;
}

void FDMXProtocolReceiverArtNet::Update(const FTimespan& SocketWaitTime)
{
	if (!Socket->Wait(ESocketWaitConditions::WaitForRead, SocketWaitTime))
	{
		return;
	}

	TSharedRef<FInternetAddr> Sender = SocketSubsystem->CreateInternetAddr();
	uint32 Size;

	while (Socket->HasPendingData(Size))
	{
		FArrayReaderPtr Reader = MakeShareable(new FArrayReader(true));
		Reader->SetNumUninitialized(FMath::Min(Size, 65507u));

		int32 Read = 0;

		if (Socket->RecvFrom(Reader->GetData(), Reader->Num(), Read, *Sender))
		{
			Reader->RemoveAt(Read, Reader->Num() - Read, false);
			DMXDataReceiveDelegate.ExecuteIfBound(Reader);
		}
	}
}
