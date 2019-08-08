// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "OSCClient.h"

#include "Common/UdpSocketBuilder.h"
#include "Sockets.h"

#include "OSCStream.h"
#include "OSCBundle.h"
#include "OSCLog.h"
#include "OSCPacket.h"


namespace
{
	static const int32 OUTPUT_BUFFER_SIZE = 1024;
} // namespace <>


UOSCClient::UOSCClient()
	: Socket(FUdpSocketBuilder(*GetName()).Build())
{
}

UOSCClient::~UOSCClient()
{
}

bool UOSCClient::SetSendIPAddress(const FString& InIPAddress, const int32 Port)
{
	IPAddress = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();

	bool bIsValidAddress = true;
	IPAddress->SetIp(*InIPAddress, bIsValidAddress);
	IPAddress->SetPort(Port);

	if (bIsValidAddress)
	{
		UE_LOG(LogOSC, Verbose, TEXT("OSCClient '%s' SetSendIpAddress: %s:%d"), *GetName(), *InIPAddress, Port);
	}
	else
	{
		UE_LOG(LogOSC, Warning, TEXT("OSCClient '%s' SetSendIpAddress Failed for input: %s:%d"), *GetName(), *InIPAddress, Port);
	}

	return bIsValidAddress;
}

void UOSCClient::GetSendIPAddress(FString& InIPAddress, int32& Port)
{
	const bool bAppendPort = false;
	InIPAddress = IPAddress->ToString(bAppendPort);
	Port = IPAddress->GetPort();
}

void UOSCClient::SendPacket(IOSCPacket& Packet)
{
	const FOSCAddress& OSCAddress = Packet.GetAddress();
	if (Packet.IsMessage() && !OSCAddress.IsValidPath())
	{
		UE_LOG(LogOSC, Warning, TEXT("Failed to write packet data. Invalid OSCAddress '%s'"), *OSCAddress.GetFullPath());
		return;
	}

	if (IPAddress)
	{
		FOSCStream Stream = FOSCStream(OUTPUT_BUFFER_SIZE);
		const uint8* DataPtr = Stream.GetData();
		Packet.WriteData(Stream);

		int32 BytesSent = 0;

		const int32 AttemptedLength = Stream.GetPosition();
		int32 Length = AttemptedLength;
		while (Length > 0)
		{
			const bool bSuccess = Socket->SendTo(DataPtr, Length, BytesSent, *IPAddress);
			if (!bSuccess || BytesSent <= 0)
			{
				UE_LOG(LogOSC, Verbose, TEXT("OSC Packet failed: Client '%s', OSCAddress '%s', Send IPAddress %s, Attempted Bytes = %d"),
					*GetName(), *OSCAddress.GetFullPath(), *IPAddress->ToString(true /*bAppendPort*/), AttemptedLength);
				return;
			}

			Length -= BytesSent;
			DataPtr += BytesSent;
		}

		UE_LOG(LogOSC, Verbose, TEXT("OSC Packet sent: Client '%s', OSCAddress '%s', Send IPAddress %s, Bytes Sent = %d"),
			*GetName(), *OSCAddress.GetFullPath(), *IPAddress->ToString(true /*bAppendPort*/), AttemptedLength);
	}
}

void UOSCClient::Stop()
{
	if (Socket)
	{
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}
}

void UOSCClient::BeginDestroy()
{
	Stop();
	Super::BeginDestroy();
}

void UOSCClient::SendOSCMessage(FOSCMessage& Message)
{
	const TSharedPtr<IOSCPacket>& Packet = Message.GetPacket();
	check(Packet.IsValid());
	SendPacket(*Packet.Get());
}

void UOSCClient::SendOSCBundle(FOSCBundle& Bundle)
{
	const TSharedPtr<IOSCPacket>& Packet = Bundle.GetPacket();
	check(Packet.IsValid());
	SendPacket(*Packet.Get());
}
