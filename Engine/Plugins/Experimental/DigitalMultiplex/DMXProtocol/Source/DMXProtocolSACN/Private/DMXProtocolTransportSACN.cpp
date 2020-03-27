// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolTransportSACN.h"
#include "DMXProtocolSACN.h"
#include "DMXProtocolTypes.h"

#include "HAL/Event.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
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
	SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	check(SocketSubsystem);

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
	FDMXPacketPtr Packet;
	while (OutboundPackages.Dequeue(Packet))
	{
		++LastSentPackage;
		int32 BytesSent = 0;
		bool bIsSent = BroadcastSocket->SendTo(Packet->Data.GetData(), Packet->Data.Num(), BytesSent, *FDMXProtocolSACN::GetUniverseAddr(Packet->Settings.GetNumberField(TEXT("UniverseID"))));
		if (!bIsSent)
		{
			ESocketErrors RecvFromError = SocketSubsystem->GetLastErrorCode();

			UE_LOG_DMXPROTOCOL(Error, TEXT("Error sending %d"),
				(uint8)RecvFromError);
		}
	}
}
