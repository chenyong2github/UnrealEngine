// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "OSCServer.h"

#include "Runtime/Core/Public/Async/TaskGraphInterfaces.h"
#include "Sockets.h"
#include "Common/UdpSocketBuilder.h"
#include "Common/UdpSocketReceiver.h"

#include "OSCStream.h"
#include "OSCMessage.h"
#include "OSCBundle.h"
#include "OSCLog.h"

#define OSC_BUFFER_FREE 0
#define OSC_BUFFER_LOCKED 1

UOSCServer::UOSCServer()
: OSCPackets(1024)
, OSCBufferLock(0)
{
}

UOSCServer::~UOSCServer()
{
	delete SocketReceiver;
	SocketReceiver = nullptr;

	if (Socket)
	{
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}
}

void UOSCServer::Listen(FIPv4Address IPAddress, uint32_t Port, bool MulticastLoopback)
{
	FUdpSocketBuilder builder(TEXT("OscListener"));
	builder.BoundToPort(Port);
	if (IPAddress.IsMulticastAddress())
	{
		builder.JoinedToGroup(IPAddress);
		if (MulticastLoopback)
		{
			builder.WithMulticastLoopback();
		}
	}
	else
	{
		builder.BoundToAddress(IPAddress);
	}

	Socket = builder.Build();
	if (Socket)
	{
		SocketReceiver = new FUdpSocketReceiver(Socket, FTimespan::FromMilliseconds(100), TEXT("OSCListener"));
		SocketReceiver->OnDataReceived().BindUObject(this, &UOSCServer::Callback);
		SocketReceiver->Start();

		UE_LOG(OSCLog, Display, TEXT("Listening to port %d"), Port);
	}
	else
	{
		UE_LOG(OSCLog, Warning, TEXT("Cannot listen to port %d"), Port);
	}
}

void UOSCServer::Callback(const FArrayReaderPtr& Data, const FIPv4Endpoint& Endpoint)
{
	TSharedPtr <FOSCPacket> packet = FOSCPacket::CreatePacket((char *)Data->GetData());

	if (packet.IsValid())
	{
		// create OSC stream
		FOSCStream stream = FOSCStream((char *)Data->GetData(), Data->Num());

		packet->ReadData(stream);

		OSCPackets.Buffer.Enqueue(MoveTemp(packet));
	}

	if (!FPlatformAtomics::InterlockedCompareExchange(&OSCBufferLock, OSC_BUFFER_LOCKED, OSC_BUFFER_FREE) &&
		!OSCPackets.Buffer.IsEmpty())
	{
		check(OSCBufferLock == OSC_BUFFER_LOCKED);
		FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(
			FSimpleDelegateGraphTask::FDelegate::CreateLambda([this]() {
			check(OSCBufferLock == OSC_BUFFER_LOCKED);
			FPlatformAtomics::InterlockedCompareExchange(&OSCBufferLock, OSC_BUFFER_FREE, OSC_BUFFER_LOCKED);
				
				TSharedPtr<FOSCPacket> packet;
				while (OSCPackets.Buffer.Dequeue(packet))
				{
					if (packet->IsMessage())
					{
						FOSCMessage message;
						message.SetPacket(StaticCastSharedPtr<FOSCMessagePacket>(packet));
						OnOscReceived.Broadcast(message.GetPacket()->GetAddress(), message);
					}
					else if (packet->IsBundle())
					{
						FOSCBundle bundle;
						bundle.SetPacket(StaticCastSharedPtr<FOSCBundlePacket>(packet));
						OnOscBundleReceived.Broadcast(bundle);
					}
				}
				OSCPackets.Buffer.Empty();
			}),
			TStatId(),
			nullptr,
			ENamedThreads::GameThread);
	}
}

void UOSCServer::Stop()
{
	delete SocketReceiver;
	SocketReceiver = nullptr;

	if (Socket)
	{
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}
}

void UOSCServer::BeginDestroy()
{
	Stop();
	Super::BeginDestroy();
}
