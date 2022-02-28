// Copyright Epic Games, Inc. All Rights Reserved.

#include "StageAppBeaconReceiver.h"

#include "Common/UdpSocketBuilder.h"
#include "IRemoteControlModule.h"
#include "MessageEndpoint.h"
#include "MessageEndpointBuilder.h"
#include "RemoteControlSettings.h"
#include "Serialization/ArrayReader.h"
#include "Serialization/ArrayWriter.h"
#include "StageAppSettings.h"

namespace StageAppBeaconReceiverConstants
{
	const uint8 ProtocolVersion = 0;
}

FStageAppBeaconReceiver::FStageAppBeaconReceiver()
	: Guid(FGuid::NewGuid())
{
}

void FStageAppBeaconReceiver::Startup()
{
	const UStageAppSettings& Settings = *GetDefault<UStageAppSettings>();
	FIPv4Address DiscoveryAddress;
	if (!FIPv4Address::Parse(Settings.DiscoveryEndpoint, DiscoveryAddress))
	{
		UE_LOG(LogRemoteControl, Error, TEXT("Failed to parse Stage App discovery endpoint address \"%s\""), *Settings.DiscoveryEndpoint);
		return;
	}

	Socket = FUdpSocketBuilder(TEXT("StageAppBeaconResponder"))
		.AsNonBlocking()
		.AsReusable()
		.BoundToPort(Settings.DiscoveryPort)
		.JoinedToGroup(DiscoveryAddress)
		.WithMulticastLoopback()
		.WithReceiveBufferSize(256);

	WebsocketPort = GetDefault<URemoteControlSettings>()->RemoteControlWebSocketServerPort;

	if (!Socket)
	{
		UE_LOG(LogRemoteControl, Warning, TEXT("StageAppBeaconResponder failed to create multicast socket"));
		return;
	}

	Thread.Reset(FRunnableThread::Create(this, TEXT("StageAppBeaconResponderThread"), 0, TPri_Normal, FPlatformAffinity::GetPoolThreadMask()));
}

void FStageAppBeaconReceiver::Shutdown()
{
	if (Thread)
	{
		Thread->Kill();
		Thread = nullptr;
	}

	if (Socket)
	{
		ISocketSubsystem::Get()->DestroySocket(Socket);
	}
}

bool FStageAppBeaconReceiver::Init()
{
	return true;
}

uint32 FStageAppBeaconReceiver::Run()
{
	const UStageAppSettings& Settings = *GetDefault<UStageAppSettings>();
	const FTimespan WaitTime = FTimespan::FromSeconds(Settings.DiscoverySocketWaitTime);

	while (!bStopping)
	{
		if (!Socket)
		{
			return 0;
		}

		Socket->Wait(ESocketWaitConditions::WaitForRead, WaitTime);
		ReceiveBeaconMessages();
	}

	return 0;
}

void FStageAppBeaconReceiver::Stop()
{
	bStopping = true;
}

void FStageAppBeaconReceiver::ReceiveBeaconMessages()
{
	if (!Socket)
	{
		return;
	}

	uint32 PendingDataSize;
	if (Socket->HasPendingData(PendingDataSize) && PendingDataSize > 0)
	{
		FArrayReader MessageData = FArrayReader(true);
		MessageData.SetNumUninitialized(PendingDataSize);

		int32 NumRead;
		TSharedRef<FInternetAddr> Source = ISocketSubsystem::Get()->CreateInternetAddr();
		if (Socket->RecvFrom(MessageData.GetData(), MessageData.Num(), NumRead, *Source) && NumRead > 0)
		{
			HandleBeaconMessage(MessageData, Source);
		}
	}
}

void FStageAppBeaconReceiver::HandleBeaconMessage(FArrayReader& MessageData, TSharedRef<FInternetAddr> Source)
{
	if (MessageData.Num() != 1)
	{
		return;
	}

	// We don't do anything with this yet, but could use it to ignore beacons from apps with incompatible protocols
	uint8 AppProtocolVersion;
	MessageData << AppProtocolVersion;

	const FString FriendlyName = GetFriendlyName();

	FArrayWriter Writer(true);

#if !PLATFORM_LITTLE_ENDIAN
	// App expects little-endian byte order
	Writer.SetByteSwapping(true);
#endif

	{
		Writer << (uint8&)StageAppBeaconReceiverConstants::ProtocolVersion;
		Writer << Guid;
		Writer << (uint32&)WebsocketPort;
		Writer << (FString&)FriendlyName;
	}

	int32 Sent;
	Socket->SendTo(Writer.GetData(), Writer.Num(), Sent, *Source);
}

FString FStageAppBeaconReceiver::GetFriendlyName() const
{
	FString FriendlyName;

	if (FParse::Value(FCommandLine::Get(), TEXT("-StageFriendlyName="), FriendlyName))
	{
		return FriendlyName;
	}

	if (FParse::Value(FCommandLine::Get(), TEXT("-CONCERTDISPLAYNAME="), FriendlyName))
	{
		return FriendlyName;
	}

	return FApp::GetSessionOwner();
}
