// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolTransportSACN.h"
#include "DMXProtocolUniverseSACN.h"
#include "DMXProtocolSACNConstants.h"
#include "DMXProtocolSACN.h"
#include "DMXProtocolSettings.h"
#include "DMXProtocolTypes.h"
#include "Interfaces/IDMXProtocol.h"
#include "Managers/DMXProtocolUniverseManager.h"
#include "DMXProtocolConstants.h"
#include "DMXStats.h"

#include "HAL/Event.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Serialization/ArrayReader.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

// Stats
DECLARE_CYCLE_STAT(TEXT("SACN Packages Sent"), STAT_SACNPackagesSent, STATGROUP_DMX);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("SACN Packages Sent Total"), STAT_SACNPackagesSentTotal, STATGROUP_DMX);

FDMXProtocolSenderSACN::FDMXProtocolSenderSACN(FSocket& InSocket, FDMXProtocolSACN* InProtocol)
	: LastSentPackage(-1)
	, bRequestingExit(false)
	, BroadcastSocket(&InSocket)
	, Protocol(InProtocol)
{
	SendingRefreshRate = DMX_MAX_REFRESH_RATE;

	SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	check(SocketSubsystem);

	WorkEvent = MakeShareable(FPlatformProcess::GetSynchEventFromPool(), [](FEvent* EventToDelete)
	{
		FPlatformProcess::ReturnSynchEventToPool(EventToDelete);
	});

	Thread = FRunnableThread::Create(this, TEXT("FDMXProtocolSenderSACN"), 128 * 1024, TPri_TimeCritical, FPlatformAffinity::GetPoolThreadMask());

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
	while (WorkEvent->Wait() && StopTaskCounter.GetValue() == 0)
	{
		// Send all packages instantly if the refresh rate is 0
		if (SendingRefreshRate <= 0)
		{
			ConsumeOutboundPackages();
		}
		// Otherwise, wait by value in refresh rate
		else
		{
			ConsumeOutboundPackages();

			// Sleep by the amount which is set in refresh rate
			FPlatformProcess::SleepNoStats(1.f / SendingRefreshRate);
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
	check(IsInGameThread());

	if (StopTaskCounter.GetValue())
	{
		return false;
	}

	{
		FScopeLock ScopeLock(&PacketCS);
		OutboundPackages.Add(Packet->UniverseID, Packet);
	}

	// Update refresh rate before triggering the loop
	const UDMXProtocolSettings* ProtocolSettings = GetDefault<UDMXProtocolSettings>();
	if (ProtocolSettings != nullptr && ProtocolSettings->IsValidLowLevel())
	{
		SendingRefreshRate = ProtocolSettings->SendingRefreshRate;
	}

	WorkEvent->Trigger();

	FDMXProtocolSACN::FOnPacketSent& OnPacketSent = Protocol->GetOnPacketSent();
	OnPacketSent.Broadcast(DMX_PROTOCOLNAME_SACN, Packet->UniverseID, Packet->Data);

	return true;
}

void FDMXProtocolSenderSACN::ConsumeOutboundPackages()
{
	FScopeLock ScopeLock(&PacketCS);

	// Send all packet from the Map
	for (const TPair<uint32, FDMXPacketPtr>& PacketPair : OutboundPackages)
	{
		const FDMXPacketPtr& Packet = PacketPair.Value;
		++LastSentPackage;
		int32 BytesSent = 0;
		if (Protocol != nullptr && Protocol->GetUniverseManager() != nullptr)
		{
			if (TSharedPtr<FDMXProtocolUniverseSACN, ESPMode::ThreadSafe> Universe = Protocol->GetUniverseManager()->GetUniverseById(Packet->UniverseID))
			{
				if (InternetAddr.IsValid())
				{
					InternetAddr->SetPort(Universe->GetPort());
					for (uint32 IpAddress : Universe->GetIpAddresses())
					{
						InternetAddr->SetIp(IpAddress);
						bool bIsSent = BroadcastSocket->SendTo(Packet->Data.GetData(), Packet->Data.Num(), BytesSent, *InternetAddr);

						if (!bIsSent)
						{
							ESocketErrors RecvFromError = SocketSubsystem->GetLastErrorCode();
							UE_LOG_DMXPROTOCOL(Error, TEXT("Error sending %d"), (uint8)RecvFromError);
						}
						else
						{
							// Stats
							SCOPE_CYCLE_COUNTER(STAT_SACNPackagesSent);
							INC_DWORD_STAT(STAT_SACNPackagesSentTotal);
						}
					}
				}
			}
		}
	}

	// Nothing to send, then stop ticking
	if (OutboundPackages.Num() == 0)
	{
		WorkEvent->Reset();
	}

	// clear all packages
	OutboundPackages.Empty();
}
