// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SessionMonitorConnection.h"

#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Engine/Engine.h"
#include "Dom/JsonObject.h"

FSessionMonitorConnection::FSessionMonitorConnection(uint16_t Port)
	: Port(Port)
	, SocketSubsystem(nullptr)
	, Socket(nullptr)
	, ReadThread(nullptr)
	, ReadThreadReady(nullptr)
{
	SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (SocketSubsystem == nullptr)
	{
		UE_LOG(PixelStreamer, Error, TEXT("FSessionMonitorConnection unable to get socket subsystem"));
		return;
	}

	// Use manual reset event, so it stays triggered.
	// This allows us to call ".Wait()" multiple times without resetting
	ReadThreadReady = FGenericPlatformProcess::GetSynchEventFromPool(true);
	ReadThread = FRunnableThread::Create(this, TEXT("SessionMonitor Connection"));
}

FSessionMonitorConnection::~FSessionMonitorConnection()
{
	// If we create and destroy this object rather fast, the read thread might 
	// still be initializing things
	ReadThreadReady->Wait();

	// First we close the socket, so the Read thread can detect it and exit
	if (Socket)
	{
		Socket->Close();
	}

	// Wait for the read thread to finish and delete it
	ReadThread->WaitForCompletion();
	delete ReadThread;

	// Now we can delete the socket
	if (Socket)
	{
		SocketSubsystem->DestroySocket(Socket);
	}

	FGenericPlatformProcess::ReturnSynchEventToPool(ReadThreadReady);
}

uint32 FSessionMonitorConnection::Run()
{
	Socket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("SessionMonitor Connection"));

	TSharedPtr<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
	bool bIsValid = false;
	Addr->SetIp(TEXT("127.0.0.1"), bIsValid);
	Addr->SetPort(Port);
	if (!Socket->Connect(*Addr))
	{
		UE_LOG(PixelStreamer, Error, TEXT("FSessionMonitorConnection could not connect to the SessionMonitor process"));
		ReadThreadReady->Trigger();
		return 0;
	}

	SendMsg(TEXT("heartbeat"));
	ReadThreadReady->Trigger();

	TArray<uint8> Incoming;
	while (true)
	{
		uint8 TmpBuf[512];
		int32 BytesRead = 0;
		if (!Socket->Recv(TmpBuf, sizeof(TmpBuf), BytesRead, ESocketReceiveFlags::None))
		{
			return 0;
		}

		//
		// Add to the incoming array, and if we detect a null character (end of string),
		// process it as a message
		const uint8* Ptr = &TmpBuf[0];
		while (BytesRead--)
		{
			Incoming.Add(*Ptr);
			if(*Ptr == 0) // End of message (null character)
			{
				ProcessMessage(UTF8_TO_TCHAR(Incoming.GetData()));
				Incoming.Empty();
			}
			Ptr++;
		}
	}
}

void FSessionMonitorConnection::ProcessMessage(const FString& Msg)
{
	// #RVF : Fill me
	UE_LOG(PixelStreamer, Log, TEXT("Received SessionMonitor message '%s'"), *Msg);
	FPlatformMisc::RequestExit(false);
}

void FSessionMonitorConnection::SendMsg(const TCHAR* Type)
{
	FString Msg = FString::Printf(TEXT("{\"type\":\"%s\"}"), Type);
	ANSICHAR* Utf8Str = TCHAR_TO_UTF8(*Msg);

	UE_LOG(PixelStreamer, Log, TEXT("Sending message to SessionMonitor. %s"), *Msg);
	int32 BytesSent = 0;
	int32 Count = strlen(Utf8Str) + 1; // +1 to include the null character
	Socket->Send((const uint8*)Utf8Str, Count, BytesSent);
	UE_LOG(PixelStreamer, Log, TEXT("Sent (%d bytes)"), BytesSent);

	if (BytesSent != Count)
	{
		UE_LOG(PixelStreamer, Error, TEXT("FSessionMonitorConnection failed to send full message to the SessionMonitor %d bytes of %d sent"), BytesSent, Count)
	}
}

void FSessionMonitorConnection::Heartbeat()
{
	if (ReadThreadReady->Wait())
	{
		SendMsg(TEXT("heartbeat"));
	}
}
