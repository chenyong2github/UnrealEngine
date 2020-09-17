// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolTransportArtNet.h"
#include "DMXProtocolArtNet.h"
#include "DMXProtocolSettings.h"
#include "Managers/DMXProtocolUniverseManager.h"
#include "DMXProtocolUniverseArtNet.h"
#include "DMXProtocolConstants.h"
#include "DMXStats.h"

#include "Common/UdpSocketSender.h"
#include "HAL/Event.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "IMessageAttachment.h"
#include "Serialization/ArrayReader.h"
#include "Serialization/ArrayWriter.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

// Stats
DECLARE_CYCLE_STAT(TEXT("Art-Net Packages Sent"), STAT_ArtNetPackagesSent, STATGROUP_DMX);
DECLARE_CYCLE_STAT(TEXT("Art-Net Packages Recieved"), STAT_ArtNetPackagesRecieved, STATGROUP_DMX);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Art-Net Packages Sent Total"), STAT_ArtNetPackagesSentTotal, STATGROUP_DMX);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Art-Net Packages Recieved Total"), STAT_ArtNetPackagesRecievedTotal, STATGROUP_DMX);

FDMXProtocolSenderArtNet::FDMXProtocolSenderArtNet(FSocket& InSocket, FDMXProtocolArtNet* InProtocol)
	: LastSentPackage(-1)
	, bRequestingExit(false)
	, BroadcastSocket(&InSocket)
	, Protocol(InProtocol)
{
	SendingRefreshRate = DMX_MAX_REFRESH_RATE;

	WorkEvent = MakeShareable(FPlatformProcess::GetSynchEventFromPool(), [](FEvent* EventToDelete)
	{
		FPlatformProcess::ReturnSynchEventToPool(EventToDelete);
	});

	Thread = FRunnableThread::Create(this, TEXT("FDMXProtocolSenderArtNet"), 128 * 1024, TPri_TimeCritical, FPlatformAffinity::GetPoolThreadMask());

	SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (SocketSubsystem != nullptr)
	{
		InternetAddr = SocketSubsystem->CreateInternetAddr();
	}
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
	check(IsInGameThread());

	if (StopTaskCounter.GetValue())
	{
		return false;
	}

	{
		FScopeLock ScopeLock(&PacketsCS);
		OutboundPackages.Add(Packet->UniverseID, Packet);
	}

	// Update refresh rate before triget update loop
	const UDMXProtocolSettings* ProtocolSettings = GetDefault<UDMXProtocolSettings>();
	if (ProtocolSettings != nullptr && ProtocolSettings->IsValidLowLevel())
	{
		SendingRefreshRate = ProtocolSettings->SendingRefreshRate;
	}

	WorkEvent->Trigger();

	FDMXProtocolArtNet::FOnPacketSent& OnPacketSent = Protocol->GetOnPacketSent();
	OnPacketSent.Broadcast(DMX_PROTOCOLNAME_ARTNET, Packet->UniverseID, Packet->Data);

	return true;
}

void FDMXProtocolSenderArtNet::ConsumeOutboundPackages()
{
	FScopeLock ScopeLock(&PacketsCS);

	// Send all packet from the Map
	for (const TPair<uint32, FDMXPacketPtr>& PacketPair : OutboundPackages)
	{
		const FDMXPacketPtr& Packet = PacketPair.Value;
		if (Packet.IsValid())
		{
			++LastSentPackage;
			int32 BytesSent = 0;
			if (Protocol != nullptr && Protocol->GetUniverseManager() != nullptr)
			{
				if (TSharedPtr<FDMXProtocolUniverseArtNet, ESPMode::ThreadSafe> Universe = Protocol->GetUniverseManager()->GetUniverseById(Packet->UniverseID))
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
								SCOPE_CYCLE_COUNTER(STAT_ArtNetPackagesSent);
								INC_DWORD_STAT(STAT_ArtNetPackagesSentTotal);
							}
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

FDMXProtocolReceiverArtNet::FDMXProtocolReceiverArtNet(FSocket& InSocket, FDMXProtocolArtNet* InProtocol, const FTimespan& InWaitTime)
	: Socket(&InSocket)
	, Stopping(false)
	, Thread(nullptr)
	, WaitTime(InWaitTime)
{
	check(Socket != nullptr);
	check(Socket->GetSocketType() == SOCKTYPE_Datagram);

	SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);

	Thread = FRunnableThread::Create(this, *ThreadName, 128 * 1024, TPri_TimeCritical, FPlatformAffinity::GetPoolThreadMask());
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
		FArrayReaderPtr Reader = MakeShared<FArrayReader, ESPMode::ThreadSafe>(true);
		Reader->SetNumUninitialized(FMath::Min(Size, 65507u));

		int32 Read = 0;

        if (Socket->RecvFrom(Reader->GetData(), Reader->Num(), Read, *Sender))
		{
            Reader->RemoveAt(Read, Reader->Num() - Read, false);
			DMXDataReceiveDelegate.ExecuteIfBound(Reader);

			// Stats
			SCOPE_CYCLE_COUNTER(STAT_ArtNetPackagesRecieved);
			INC_DWORD_STAT(STAT_ArtNetPackagesRecievedTotal);
		}
	}
}
