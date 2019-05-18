// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "HttpListener.h"
#include "HttpConnection.h"
#include "HttpRequestHandler.h"
#include "HttpRouter.h"

#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"

DEFINE_LOG_CATEGORY(LogHttpListener)

FHttpListener::FHttpListener(uint32 Port)
{ 
	check(Port > 0);
	ListenPort = Port;
	Router = MakeShared<FHttpRouter>();
}

FHttpListener::~FHttpListener() 
{ 
	check(nullptr == ListenSocket);
	check(!bIsListening);

	const bool bRequestGracefulExit = false;
	for (const auto& Connection : Connections)
	{
		Connection->RequestDestroy(bRequestGracefulExit);
	}
	Connections.Empty();
}

// --------------------------------------------------------------------------------------------
// Public Interface
// --------------------------------------------------------------------------------------------
bool FHttpListener::StartListening()
{
	check(nullptr == ListenSocket);
	check(!bIsListening);
	bIsListening = true;

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (nullptr == SocketSubsystem)
	{
		UE_LOG(LogHttpListener, Error, 
			TEXT("HttpListener - SocketSubsystem Initialization Failed"));
		return false;
	}

	ListenSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("HttpListenerSocket"));
	if (nullptr == ListenSocket)
	{
		UE_LOG(LogHttpListener, Error, 
			TEXT("HttpListener - Unable to allocate stream socket"));
		return false;
	}
	ListenSocket->SetNonBlocking(true);

	// Bind to Localhost/Caller-defined port
	TSharedRef<FInternetAddr> LocalhostAddr = SocketSubsystem->CreateInternetAddr();
	LocalhostAddr->SetAnyAddress();
	LocalhostAddr->SetPort(ListenPort);
	if (!ListenSocket->Bind(*LocalhostAddr))
	{
		UE_LOG(LogHttpListener, Error, 
			TEXT("HttpListener unable to bind to %s"), 
			*LocalhostAddr->ToString(true));
		return false;
	}

	int32 ActualBufferSize;
	ListenSocket->SetSendBufferSize(ListenerBufferSize, ActualBufferSize);
	if (ActualBufferSize != ListenerBufferSize)
	{
		UE_LOG(LogHttpListener, Warning, 
			TEXT("HttpListener unable to set desired buffer size (%d): Limited to %d"),
			ListenerBufferSize, ActualBufferSize);
	}

	if (!ListenSocket->Listen(ListenerConnectionBacklogSize))
	{
		UE_LOG(LogHttpListener, Error, 
			TEXT("HttpListener unable to listen on socket"));
		return false;
	}

	UE_LOG(LogHttpListener, Log, 
		TEXT("Created new HttpListener on port %u"), ListenPort);
	return true;
}

void FHttpListener::StopListening()
{
	check(bIsListening);

	// Tear down our top-level listener first
	if (ListenSocket)
	{
		UE_LOG(LogHttpListener, Log,
			TEXT("HttListener stopping listening on Port %u"), ListenPort);

		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (SocketSubsystem)
		{
			SocketSubsystem->DestroySocket(ListenSocket);
		}
		ListenSocket = nullptr;
	}
	bIsListening = false;

	const bool bRequestGracefulExit = true;
	for (const auto& Connection : Connections)
	{
		Connection->RequestDestroy(bRequestGracefulExit);
	}
}

void FHttpListener::Tick(float DeltaTime)
{
	// Accept new connections
	AcceptConnections(MaxConnectionsToAcceptPerFrame);

	// Tick Connections
	TickConnections(DeltaTime);

	// Remove any destroyed connections
	RemoveDestroyedConnections();
}

bool FHttpListener::HasPendingConnections() const 
{
	for (const auto& Connection : Connections)
	{
		switch (Connection->GetState())
		{
		case EHttpConnectionState::Reading:
		case EHttpConnectionState::AwaitingProcessing:
		case EHttpConnectionState::Writing:
			return true;
		}
	}
	return false;
}

// --------------------------------------------------------------------------------------------
// Private Implementation
// --------------------------------------------------------------------------------------------
void FHttpListener::AcceptConnections(uint32 MaxConnectionsToAccept)
{
	check(ListenSocket);

	for (uint32 i = 0; i < MaxConnectionsToAccept; ++i)
	{
		// Check pending prior to Accept()ing
		bool bHasPendingConnection = false;
		if (!ListenSocket->HasPendingConnection(bHasPendingConnection))
		{
			UE_LOG(LogHttpListener, 
				Error, TEXT("ListenSocket failed to query pending connection"));
			return;
		}

		if (bHasPendingConnection)
		{
			FSocket* IncomingConnection = ListenSocket->Accept(TEXT("HttpRequest"));

			if (nullptr == IncomingConnection)
			{
				ESocketErrors ErrorCode = ESocketErrors::SE_NO_ERROR;
				FString ErrorStr = TEXT("SocketSubsystem Unavialble");

				ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
				if (SocketSubsystem)
				{
					ErrorCode = SocketSubsystem->GetLastErrorCode();
					ErrorStr = SocketSubsystem->GetSocketError();
				}
				UE_LOG(LogHttpListener, Error,
					TEXT("Error accepting expected connection [%d] %s"), (int32)ErrorCode, *ErrorStr);
				return;
			}

			IncomingConnection->SetNonBlocking(true);
			TSharedPtr<FHttpConnection> Connection = 
				MakeShared<FHttpConnection>(IncomingConnection, Router, ListenPort, NumConnectionsAccepted++);
			Connections.Add(Connection);
		}
	}
}

void FHttpListener::TickConnections(float DeltaTime)
{
	for (const auto& Connection : Connections)
	{
		check(Connection.IsValid());

		switch (Connection->GetState())
		{
		case EHttpConnectionState::AwaitingRead:
		case EHttpConnectionState::Reading:
			Connection->Tick(DeltaTime);
			break;
		}
	}

	for (const auto& Connection : Connections)
	{
		check(Connection.IsValid());

		switch (Connection->GetState())
		{
		case EHttpConnectionState::Writing:
			Connection->Tick(DeltaTime);
			break;
		}
	}
}

void FHttpListener::RemoveDestroyedConnections()
{
	for (auto ConnectionsIter = Connections.CreateIterator(); ConnectionsIter; ++ConnectionsIter)
	{
		// Remove any destroyed connections
		if (EHttpConnectionState::Destroyed == ConnectionsIter->Get()->GetState())
		{
			ConnectionsIter.RemoveCurrent();
			continue;
		}
	}
}