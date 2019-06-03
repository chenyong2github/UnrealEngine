// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "OSCClient.h"

#include "Common/UdpSocketBuilder.h"
#include "Sockets.h"

#include "OSCStream.h"
#include "OSCBundle.h"
#include "OSCLog.h"

UOSCClient::UOSCClient()
	: Socket(FUdpSocketBuilder(TEXT("OscSender")).Build())
{
	//
}

void UOSCClient::SetTarget(FIPv4Address InIPAddress, uint32_t Port)
{
	this->IPAddress = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();

	this->IPAddress->SetIp(InIPAddress.Value);
	this->IPAddress->SetPort(Port);

	UE_LOG(OSCLog, Display, TEXT("Set target port: %d"), Port);
}

void UOSCClient::SendPacket(FOSCPacket* Packet, char* Data, int32 Size)
{
	if (IPAddress)
	{
		FOSCStream stream = FOSCStream(Data, Size);

		Packet->WriteData(stream);

		int32 length = stream.GetPosition();

		int32 bytesSent = 0;
		while (length > 0)
		{
			Socket->SendTo((uint8*)Data, length, bytesSent, *IPAddress);

			length -= bytesSent;
			Data += bytesSent;
		}
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

void UOSCClient::SendOSCMessage(FString Address, UPARAM(ref) FOSCMessage& Msg)
{
	static const int OUTPUT_BUFFER_SIZE = 1024;
	char buffer[OUTPUT_BUFFER_SIZE];

	Msg.GetPacket()->SetAddress(Address);

	SendPacket(&(*Msg.GetPacket()), &buffer[0], OUTPUT_BUFFER_SIZE);
}

void UOSCClient::SendOSCBundle(UPARAM(ref) FOSCBundle& Bundle)
{
	static const int OUTPUT_BUFFER_SIZE = 1024;
	char buffer[OUTPUT_BUFFER_SIZE];

	SendPacket(&(*Bundle.GetPacket()), &buffer[0], OUTPUT_BUFFER_SIZE);
}
