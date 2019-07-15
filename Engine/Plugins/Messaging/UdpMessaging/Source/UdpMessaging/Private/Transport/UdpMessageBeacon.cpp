// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Transport/UdpMessageBeacon.h"
#include "UdpMessagingPrivate.h"

#include "HAL/Event.h"
#include "HAL/RunnableThread.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "IPAddress.h"
#include "Serialization/ArrayWriter.h"
#include "Sockets.h"

#if WITH_TARGETPLATFORM_SUPPORT
#include "Misc/CoreMisc.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Interfaces/ITargetPlatform.h"
#endif

/* FUdpMessageHelloSender static initialization
 *****************************************************************************/

const FTimespan FUdpMessageBeacon::IntervalPerEndpoint = FTimespan::FromMilliseconds(200);
const FTimespan FUdpMessageBeacon::MinimumInterval = FTimespan::FromMilliseconds(1000);


/* FUdpMessageHelloSender structors
 *****************************************************************************/

FUdpMessageBeacon::FUdpMessageBeacon(FSocket* InSocket, const FGuid& InSocketId, const FIPv4Endpoint& InMulticastEndpoint, const TArray<FIPv4Endpoint>& InStaticEndpoints)
	: BeaconInterval(MinimumInterval)
	, LastEndpointCount(1)
	, LastHelloSent(FDateTime::MinValue())
	, NextHelloTime(FDateTime::UtcNow())
	, NodeId(InSocketId)
	, Socket(InSocket)
	, Stopping(false)
{
	EndpointLeftEvent = FPlatformProcess::GetSynchEventFromPool(false);
	MulticastAddress = InMulticastEndpoint.ToInternetAddr();
	for (const FIPv4Endpoint& Endpoint : InStaticEndpoints)
	{
		StaticAddresses.Add(Endpoint.ToInternetAddr());
	}

	// if we don't have a local endpoint address in our static discovery endpoint, add one.
	// this is to properly discover other message bus process bound on the loopback address (i.e for the launcher not to trigger firewall)
	// note: this will only properly work if the socket is not bound (or bound to the any address)
	FIPv4Endpoint LocalEndpoint(FIPv4Address(127, 0, 0, 1), InMulticastEndpoint.Port);
	if (!InStaticEndpoints.Contains(LocalEndpoint))
	{
		StaticAddresses.Add(LocalEndpoint.ToInternetAddr());
	}

#if WITH_TARGETPLATFORM_SUPPORT
	if( ITargetPlatformManagerModule* TargetPlatformManager = GetTargetPlatformManager() )
	{
		TArray<ITargetPlatform*> Platforms = TargetPlatformManager->GetTargetPlatforms();
		for (ITargetPlatform* Platform : Platforms)
		{
			// set up target platform callbacks
			Platform->OnDeviceDiscovered().AddRaw(this, &FUdpMessageBeacon::HandleTargetPlatformDeviceDiscovered);
			Platform->OnDeviceLost().AddRaw(this, &FUdpMessageBeacon::HandleTargetPlatformDeviceLost);

			TArray<ITargetDevicePtr> Devices;
			Platform->GetAllDevices( Devices );
			for( ITargetDevicePtr Device : Devices )
			{
				if (Device.IsValid())
				{
					HandleTargetPlatformDeviceDiscovered(Device.ToSharedRef());
				}
			}
		}
		ProcessPendingEndpoints();
	}
#endif //WITH_TARGETPLATFORM_SUPPORT

	Thread = FRunnableThread::Create(this, TEXT("FUdpMessageBeacon"), 128 * 1024, TPri_AboveNormal, FPlatformAffinity::GetPoolThreadMask());
}


FUdpMessageBeacon::~FUdpMessageBeacon()
{
	if (Thread != nullptr)
	{
		Thread->Kill(true);
		delete Thread;
	}

	MulticastAddress = nullptr;
	StaticAddresses.Empty();

	FPlatformProcess::ReturnSynchEventToPool(EndpointLeftEvent);
	EndpointLeftEvent = nullptr;

#if WITH_TARGETPLATFORM_SUPPORT
	if( ITargetPlatformManagerModule* TargetPlatformManager = GetTargetPlatformManager() )
	{
		TArray<ITargetPlatform*> Platforms = TargetPlatformManager->GetTargetPlatforms();

		for (int32 PlatformIndex = 0; PlatformIndex < Platforms.Num(); ++PlatformIndex)
		{
			// set up target platform callbacks
			ITargetPlatform* Platform = Platforms[PlatformIndex];
			Platform->OnDeviceDiscovered().RemoveAll(this);
			Platform->OnDeviceLost().RemoveAll(this);

		}
	}
#endif //WITH_TARGETPLATFORM_SUPPORT
}


/* FUdpMessageHelloSender interface
 *****************************************************************************/

void FUdpMessageBeacon::SetEndpointCount(int32 EndpointCount)
{
	check(EndpointCount > 0);

	if (EndpointCount < LastEndpointCount)
	{
		FDateTime CurrentTime = FDateTime::UtcNow();

		// adjust the send interval for reduced number of endpoints
		NextHelloTime = CurrentTime + (EndpointCount / LastEndpointCount) * (NextHelloTime - CurrentTime);
		LastHelloSent = CurrentTime - (EndpointCount / LastEndpointCount) * (CurrentTime - LastHelloSent);
		LastEndpointCount = EndpointCount;

		EndpointLeftEvent->Trigger();
	}
}


/* FRunnable interface
 *****************************************************************************/

FSingleThreadRunnable* FUdpMessageBeacon::GetSingleThreadInterface()
{
	return this;
}


bool FUdpMessageBeacon::Init()
{
	return true;
}


uint32 FUdpMessageBeacon::Run()
{
	while (!Stopping)
	{
		FDateTime CurrentTime = FDateTime::UtcNow();
		Update(CurrentTime, BeaconInterval);
		// Clamp the wait time to a positive value of at least 100ms.
		uint32 WaitTimeMs = (uint32)FMath::Max((NextHelloTime - CurrentTime).GetTotalMilliseconds(), 100.0);
		EndpointLeftEvent->Wait(WaitTimeMs);
	}

	SendSegment(EUdpMessageSegments::Bye, BeaconInterval);

	return 0;
}


void FUdpMessageBeacon::Stop()
{
	Stopping = true;
}


/* FUdpMessageHelloSender implementation
 *****************************************************************************/

bool FUdpMessageBeacon::SendSegment(EUdpMessageSegments SegmentType, const FTimespan& SocketWaitTime)
{
	FUdpMessageSegment::FHeader Header;
	{
		Header.SenderNodeId = NodeId;
		Header.ProtocolVersion = UDP_MESSAGING_TRANSPORT_PROTOCOL_VERSION;
		Header.SegmentType = SegmentType;
	}

	FArrayWriter Writer;
	{
		Writer << Header;
		Writer << NodeId;
	}

	int32 Sent;

	if (!Socket->Wait(ESocketWaitConditions::WaitForWrite, SocketWaitTime))
	{
		return false; // socket not ready for sending
	}

	if (!Socket->SendTo(Writer.GetData(), Writer.Num(), Sent, *MulticastAddress))
	{
		return false; // send failed
	}

	return true;
}


bool FUdpMessageBeacon::SendPing(const FTimespan& SocketWaitTime)
{
	FUdpMessageSegment::FHeader Header;
	{
		Header.SenderNodeId = NodeId;
		// Pings were introduced at ProtocolVersion 11 and those messages needs to be send with that header to allow backward and forward discoverability
		Header.ProtocolVersion = 11;
		Header.SegmentType = EUdpMessageSegments::Ping;
	}
	uint8 ActualProtocolVersion = UDP_MESSAGING_TRANSPORT_PROTOCOL_VERSION;

	FArrayWriter Writer;
	{
		Writer << Header;
		Writer << NodeId;
		Writer << ActualProtocolVersion; // Send our actual Protocol version as part of the ping message
	}


	if (!Socket->Wait(ESocketWaitConditions::WaitForWrite, SocketWaitTime))
	{
		return false; // socket not ready for sending
	}

	int32 Sent;
	for (const auto& StaticAddress : StaticAddresses)
	{
		if (!Socket->SendTo(Writer.GetData(), Writer.Num(), Sent, *StaticAddress))
		{
			return false; // send failed
		}

	}
	return true;
}


void FUdpMessageBeacon::Update(const FDateTime& CurrentTime, const FTimespan& SocketWaitTime)
{
#if WITH_TARGETPLATFORM_SUPPORT
	ProcessPendingEndpoints();
#endif


	if (CurrentTime < NextHelloTime)
	{
		return;
	}

	BeaconInterval = FMath::Max(MinimumInterval, IntervalPerEndpoint * LastEndpointCount);

	if (SendSegment(EUdpMessageSegments::Hello, SocketWaitTime))
	{
		NextHelloTime = CurrentTime + BeaconInterval;
	}
	SendPing(SocketWaitTime);
}


#if WITH_TARGETPLATFORM_SUPPORT
void FUdpMessageBeacon::HandleTargetPlatformDeviceDiscovered( TSharedRef<class ITargetDevice, ESPMode::ThreadSafe> DiscoveredDevice)
{
	TSharedPtr<const FInternetAddr> DeviceStaticEndpoint = DiscoveredDevice->GetMessagingEndpoint();
	if (DeviceStaticEndpoint.IsValid() )
	{
		FPendingEndpoint PendingEndpoint;
		PendingEndpoint.StaticAddress = DeviceStaticEndpoint;
		PendingEndpoint.bAdd = true;
		PendingEndpoints.Enqueue(PendingEndpoint);
	}
}
#endif //WITH_TARGETPLATFORM_SUPPORT

#if WITH_TARGETPLATFORM_SUPPORT
void FUdpMessageBeacon::HandleTargetPlatformDeviceLost( TSharedRef<class ITargetDevice, ESPMode::ThreadSafe> LostDevice)
{
	TSharedPtr<const FInternetAddr> DeviceStaticEndpoint = LostDevice->GetMessagingEndpoint();
	if (DeviceStaticEndpoint.IsValid() )
	{
		FPendingEndpoint PendingEndpoint;
		PendingEndpoint.StaticAddress = DeviceStaticEndpoint;
		PendingEndpoint.bAdd = false;
		PendingEndpoints.Enqueue(PendingEndpoint);
	}
}
#endif //WITH_TARGETPLATFORM_SUPPORT

#if WITH_TARGETPLATFORM_SUPPORT
void FUdpMessageBeacon::ProcessPendingEndpoints()
{
	// process any pending static endpoint changes from target platform device changes
	FPendingEndpoint PendingEndpoint;
	while( PendingEndpoints.Dequeue(PendingEndpoint) )
	{
		if( !PendingEndpoint.StaticAddress.IsValid() )
		{
			continue;
		}

		if( PendingEndpoint.bAdd )
		{
			StaticAddresses.Add(PendingEndpoint.StaticAddress->Clone());
		}
		else
		{
			StaticAddresses.RemoveAll([&]( TSharedPtr<FInternetAddr> Addr)
			{
				return PendingEndpoint.StaticAddress->CompareEndpoints(*Addr.Get());
			});
		}
	}
}
#endif //WITH_TARGETPLATFORM_SUPPORT



/* FSingleThreadRunnable interface
 *****************************************************************************/

void FUdpMessageBeacon::Tick()
{
	Update(FDateTime::UtcNow(), FTimespan::Zero());
}
