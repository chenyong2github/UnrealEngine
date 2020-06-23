// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolTransportSACN.h"
#include "DMXProtocolUniverseSACN.h"
#include "DMXProtocolSACNConstants.h"
#include "DMXProtocolSACN.h"
#include "DMXProtocolTypes.h"
#include "Interfaces/IDMXProtocol.h"
#include "Managers/DMXProtocolUniverseManager.h"

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

	// Class
	
	if (SocketSubsystem != nullptr)
	{
		InternetAddr = SocketSubsystem->CreateInternetAddr();
	}
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

	auto& OutputEvent = Protocol->GetOnOutputSentEvent();
	if (OutputEvent.IsBound())
	{
		OutputEvent.Broadcast(DMX_PROTOCOLNAME_SACN, Packet->UniverseID, Packet->Data);
	}
	
	return true;
}

void FDMXProtocolSenderSACN::ConsumeOutboundPackages()
{
	FDMXPacketPtr Packet;
	while (OutboundPackages.Dequeue(Packet))
	{
		if (Packet.IsValid())
		{
			++LastSentPackage;
			int32 BytesSent = 0;
			if (Protocol != nullptr && Protocol->GetUniverseManager() != nullptr)
			{
				if (TSharedPtr<FDMXProtocolUniverseSACN, ESPMode::ThreadSafe> Universe = Protocol->GetUniverseManager()->GetUniverseById(Packet->UniverseID))
				{
					if (InternetAddr.IsValid())
					{
						InternetAddr->SetPort(Universe->GetPort());
						InternetAddr->SetIp(Universe->GetIpAddress());
						bool bIsSent = BroadcastSocket->SendTo(Packet->Data.GetData(), Packet->Data.Num(), BytesSent, *InternetAddr);

						if (!bIsSent)
						{
							ESocketErrors RecvFromError = SocketSubsystem->GetLastErrorCode();
							UE_LOG_DMXPROTOCOL(Error, TEXT("Error sending %d"), (uint8)RecvFromError);
						}
					}
				}
			}
		}
	}
}
