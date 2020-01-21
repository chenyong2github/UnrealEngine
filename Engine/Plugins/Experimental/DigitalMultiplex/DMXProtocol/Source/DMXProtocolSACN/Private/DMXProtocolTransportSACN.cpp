// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolTransportSACN.h"
#include "DMXProtocolSACN.h"

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

FDMXProtocolSenderSACN::FDMXProtocolSenderSACN(FSocket& InSocket, FDMXProtocolSACN* InProtocol)
	: LastSentPackage(-1)
	, bRequestingExit(false)
	, BroadcastSocket(&InSocket)
	, Protocol(InProtocol)
{
	WorkEvent = MakeShareable(FPlatformProcess::GetSynchEventFromPool(), [](FEvent* EventToDelete)
	{
		FPlatformProcess::ReturnSynchEventToPool(EventToDelete);
	});

	Thread = FRunnableThread::Create(this, TEXT("FDMXProtocolSenderSACN"), 128 * 1024, TPri_BelowNormal, FPlatformAffinity::GetPoolThreadMask());
}

FDMXProtocolSenderSACN::~FDMXProtocolSenderSACN()
{
	// Shut Down worker thread
	if (Thread != nullptr)
	{
		Thread->Kill(true);
		delete Thread;
	}
}

bool FDMXProtocolSenderSACN::Init()
{
	return true;
}

uint32 FDMXProtocolSenderSACN::Run()
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

void FDMXProtocolSenderSACN::Stop()
{
	StopTaskCounter.Increment();

	if (WorkEvent)
	{
		WorkEvent->Trigger();
		Thread->WaitForCompletion();
	}
}

void FDMXProtocolSenderSACN::Exit()
{
}

void FDMXProtocolSenderSACN::Tick()
{
	ConsumeOutboundPackages();
}

FSingleThreadRunnable* FDMXProtocolSenderSACN::GetSingleThreadInterface()
{
	return this;
}

bool FDMXProtocolSenderSACN::EnqueueOutboundPackage(FDMXPacketPtr Packet)
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

void FDMXProtocolSenderSACN::ConsumeOutboundPackages()
{
	FScopeLock ScopeLock(&CriticalSection);

	FDMXPacketPtr Packet;
	while (OutboundPackages.Dequeue(Packet))
	{
		++LastSentPackage;
		int32 BytesSent = 0;
		bool bIsSent = BroadcastSocket->SendTo(Packet->Data.GetData(), Packet->Data.Num(), BytesSent, *FDMXProtocolSACN::GetUniverseAddr(Packet->Settings.GetNumberField(TEXT("UniverseID"))));
		if (!bIsSent)
		{
			UE_LOG_DMXPROTOCOL(Error, TEXT("Error sending %d"), bIsSent);
		}
	}
}

FDMXProtocolReceiverSACN::FDMXProtocolReceiverSACN(FSocket& InSocket, const FTimespan& InWaitTime)
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

FDMXProtocolReceiverSACN::~FDMXProtocolReceiverSACN()
{
	if (Thread != nullptr)
	{
		Thread->Kill(true);
		delete Thread;
	}
}

FOnDMXDataReceived& FDMXProtocolReceiverSACN::OnDataReceived()
{
	return DMXDataReceiveDelegate;
}

FRunnableThread* FDMXProtocolReceiverSACN::GetThread() const
{
	return Thread;
}

bool FDMXProtocolReceiverSACN::Init()
{
	return true;
}

uint32 FDMXProtocolReceiverSACN::Run()
{
	while (!Stopping)
	{
		Update(WaitTime);
	}

	return 0;
}

void FDMXProtocolReceiverSACN::Stop()
{
	Stopping = true;
}

void FDMXProtocolReceiverSACN::Exit()
{
}

void FDMXProtocolReceiverSACN::Tick()
{
	Update(FTimespan::Zero());
}

FSingleThreadRunnable* FDMXProtocolReceiverSACN::GetSingleThreadInterface()
{
	return this;
}

void FDMXProtocolReceiverSACN::Update(const FTimespan& SocketWaitTime)
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
