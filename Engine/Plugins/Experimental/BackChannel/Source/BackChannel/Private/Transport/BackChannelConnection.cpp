// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BackChannel/Private/Transport/BackChannelConnection.h"
#include "BackChannel/Private/BackChannelCommon.h"
#include "BackChannel/Transport/IBackChannelTransport.h"
#include "Common/TcpSocketBuilder.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeLock.h"
#include "CoreGlobals.h"
#include "Misc/ConfigCacheIni.h"

DECLARE_DWORD_COUNTER_STAT(TEXT("BCBytesSent"), STAT_BackChannelBytesSent, STATGROUP_Game);
DECLARE_DWORD_COUNTER_STAT(TEXT("BCBytesRecv"), STAT_BackChannelBytesRecv, STATGROUP_Game);

int32 FBackChannelConnection::SendBufferSize = 2 * 1024 * 1024;
int32 FBackChannelConnection::ReceiveBufferSize = 2 * 1024 * 1024;

int32 GBackChannelLogPackets = 0;
static FAutoConsoleVariableRef BCCVarLogPackets(
	TEXT("backchannel.logpackets"), GBackChannelLogPackets,
	TEXT("Logs incoming packets"),
	ECVF_Default);

int32 GBackChannelLogErrors = 1;
static FAutoConsoleVariableRef BCCVarLogErrors(
	TEXT("backchannel.logerrors"), GBackChannelLogErrors,
	TEXT("Logs packet errors"),
	ECVF_Default);

FBackChannelConnection::FBackChannelConnection()
{
	Socket = nullptr;
	IsListener = false;
	PacketsReceived = 0;
	// Allow the app to override
	GConfig->GetInt(TEXT("BackChannel"), TEXT("SendBufferSize"), SendBufferSize, GEngineIni);
	GConfig->GetInt(TEXT("BackChannel"), TEXT("RecvBufferSize"), ReceiveBufferSize, GEngineIni);
}

FBackChannelConnection::~FBackChannelConnection()
{
	if (Socket)
	{
		Close();
	}
}

/* Todo - Proper stats */
uint32	FBackChannelConnection::GetPacketsReceived() const
{
	return PacketsReceived;
}

bool FBackChannelConnection::IsConnected() const
{
	FBackChannelConnection* NonConstThis = const_cast<FBackChannelConnection*>(this);
	FScopeLock Lock(&NonConstThis->SocketMutex);
	return Socket != nullptr && Socket->GetConnectionState() == ESocketConnectionState::SCS_Connected;
}

bool FBackChannelConnection::IsListening() const
{
	return IsListener;
}

FString	FBackChannelConnection::GetDescription() const
{
	FBackChannelConnection* NonConstThis = const_cast<FBackChannelConnection*>(this);
	FScopeLock Lock(&NonConstThis->SocketMutex);

	return Socket ? Socket->GetDescription() : TEXT("No Socket");
}

void FBackChannelConnection::Close()
{
	FScopeLock Lock(&SocketMutex);
	if (Socket)
	{
		UE_LOG(LogBackChannel, Log, TEXT("Closing connection %s"), *Socket->GetDescription());
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
		PacketsReceived = 0;
	}
}

void FBackChannelConnection::CloseWithError(const TCHAR* Error, FSocket* InSocket)
{
	const TCHAR* SocketErr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetSocketError(SE_GET_LAST_ERROR_CODE);

	if (InSocket == nullptr)
	{
		InSocket = Socket;
	}

	FString SockDesc = InSocket != nullptr ? InSocket->GetDescription() : TEXT("(No Socket)");

	UE_LOG(LogBackChannel, Error, TEXT("%s, Err: %s, Socket:%s"), Error, SocketErr, *SockDesc);

	Close();
}

bool FBackChannelConnection::Connect(const TCHAR* InEndPoint)
{
	FScopeLock Lock(&SocketMutex);

	if (IsConnected())
	{
		Close();
	}

	IsAttemptingConnection = true;

	FString LocalEndPoint = InEndPoint;

	FSocket* NewSocket = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateSocket(NAME_Stream, TEXT("FBackChannelConnection Client Socket"));
	if (NewSocket)
	{
		NewSocket->SetNonBlocking();

		int32 NewSize = 0;
		NewSocket->SetSendBufferSize(SendBufferSize, NewSize);
		if (NewSize != SendBufferSize)
		{
			UE_LOG(LogBackChannel, Log, TEXT("SetSendBufferSize requested (%d) size but got (%d) size"), SendBufferSize, NewSize);
		}
		NewSocket->SetReceiveBufferSize(ReceiveBufferSize, NewSize);
		if (NewSize != ReceiveBufferSize)
		{
			UE_LOG(LogBackChannel, Log, TEXT("SetReceiveBufferSize requested (%d) size but got (%d) size"), SendBufferSize, NewSize);
		}

		bool Success = false;

		FIPv4Endpoint EndPointv4;
		// Check for a valid IPv4 address string
		if (FIPv4Endpoint::Parse(LocalEndPoint, EndPointv4))
		{
			Success = NewSocket->Connect(*EndPointv4.ToInternetAddr());
		}

		if (!Success)
		{
			ESocketErrors LastErr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLastErrorCode();

			if (LastErr == SE_EINPROGRESS || LastErr == SE_EWOULDBLOCK)
			{
				Success = true;
			}
			else
			{
				UE_LOG(LogBackChannel, Log, TEXT("Connect failed with error code (%d) error (%s)"), LastErr, ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetSocketError(LastErr));
			}
		}

		if (Success)
		{
			UE_LOG(LogBackChannel, Log, TEXT("Opening connection to %s (localport: %d)"), *NewSocket->GetDescription(), NewSocket->GetPortNo());
			Attach(NewSocket);
		}
		else
		{
			CloseWithError(*FString::Printf(TEXT("Failed to open connection to %s."), InEndPoint), NewSocket);
			ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(NewSocket);
		}
	}

	return Socket != nullptr;
}

bool FBackChannelConnection::Listen(const int16 Port)
{
	FScopeLock Lock(&SocketMutex);

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	FSocket* NewSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("FBackChannelConnection Client Socket"));
	if (NewSocket != nullptr)
	{
		FIPv4Endpoint Endpoint(FIPv4Address::Any, Port);

		bool Error = !NewSocket->SetReuseAddr(true);

		if (!Error)
		{
			Error = !NewSocket->SetRecvErr();
		}

		if (!Error)
		{
			Error = !NewSocket->SetNonBlocking();
		}

		if (!Error)
		{
			int32 NewSize = 0;
			NewSocket->SetSendBufferSize(SendBufferSize, NewSize);
			if (NewSize != SendBufferSize)
			{
				UE_LOG(LogBackChannel, Log, TEXT("SetSendBufferSize requested (%d) size but got (%d) size"), SendBufferSize, NewSize);
			}
			NewSocket->SetReceiveBufferSize(ReceiveBufferSize, NewSize);
			if (NewSize != ReceiveBufferSize)
			{
				UE_LOG(LogBackChannel, Log, TEXT("SetReceiveBufferSize requested (%d) size but got (%d) size"), SendBufferSize, NewSize);
			}
		}

		if (!Error)
		{
			Error = !NewSocket->Bind(*Endpoint.ToInternetAddr());
		}

		if (!Error)
		{
			Error = !NewSocket->Listen(8);
		}

		if (Error)
		{
			const TCHAR* SocketErr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetSocketError(SE_GET_LAST_ERROR_CODE);
			GLog->Logf(TEXT("Failed to create the listen socket as configured. %s"), SocketErr);

			SocketSubsystem->DestroySocket(NewSocket);

			NewSocket = nullptr;
		}
	}

	if (NewSocket == nullptr)
	{
		const TCHAR* SocketErr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetSocketError(SE_GET_LAST_ERROR_CODE);

		UE_LOG(LogBackChannel, Error, TEXT("Failed to open socket on port %d. Err: %s"), Port, SocketErr);
		CloseWithError(*FString::Printf(TEXT("Failed to start listening on port %d"), Port));
	}
	else
	{
		UE_LOG(LogBackChannel, Log, TEXT("Listening on %s (localport: %d)"), *NewSocket->GetDescription(), NewSocket->GetPortNo());
		Attach(NewSocket);
		IsListener = true;
	}

	return NewSocket != nullptr;
}

bool FBackChannelConnection::WaitForConnection(double InTimeout, TFunction<bool(TSharedRef<IBackChannelConnection>)> InDelegate)
{
	FScopeLock Lock(&SocketMutex);

	if (!Socket)
	{
		UE_LOG(LogBackChannel, Error, TEXT("Connection has no socket. Call Listen/Connect before WaitForConnection"));
		return false;
	}

	FTimespan SleepTime = FTimespan(0, 0, InTimeout);

	// handle incoming connections

	bool CheckSucceeded = false;
	bool HasConnection = false;

	if (IsListener)
	{
		CheckSucceeded = Socket->WaitForPendingConnection(HasConnection, SleepTime);
	}
	else
	{
		ESocketConnectionState State = Socket->GetConnectionState();

		if (State == ESocketConnectionState::SCS_ConnectionError)
		{
			const TCHAR* SocketErr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetSocketError(SE_GET_LAST_ERROR_CODE);
			UE_LOG(LogBackChannel, Warning, TEXT("Socket has error %s"), SocketErr);
		}
		else
		{
			CheckSucceeded = true;
			HasConnection = Socket->Wait(ESocketWaitConditions::WaitForWrite, SleepTime);
		}
	}

	if (CheckSucceeded)
	{
		if (HasConnection)
		{
			UE_LOG(LogBackChannel, Log, TEXT("Found connection on %s"), *Socket->GetDescription());

			if (IsListener == false)
			{
				InDelegate(AsShared());
			}
			else
			{
				TSharedRef<FInternetAddr> RemoteAddress = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();

				FSocket* ConnectionSocket = Socket->Accept(*RemoteAddress, TEXT("RemoteConnection"));

				if (ConnectionSocket != nullptr)
				{
					// Each platform can inherit different socket options from the listen socket so set ours again
					{
						ConnectionSocket->SetNonBlocking();

						int32 NewSize = 0;
						ConnectionSocket->SetSendBufferSize(SendBufferSize, NewSize);
						if (NewSize != SendBufferSize)
						{
							UE_LOG(LogBackChannel, Log, TEXT("SetSendBufferSize requested (%d) size but got (%d) size"), SendBufferSize, NewSize);
						}
						ConnectionSocket->SetReceiveBufferSize(ReceiveBufferSize, NewSize);
						if (NewSize != ReceiveBufferSize)
						{
							UE_LOG(LogBackChannel, Log, TEXT("SetReceiveBufferSize requested (%d) size but got (%d) size"), SendBufferSize, NewSize);
						}
					}

					TSharedRef<FBackChannelConnection> BCConnection = MakeShareable(new FBackChannelConnection);
					BCConnection->Attach(ConnectionSocket);

					if (InDelegate(BCConnection) == false)
					{
						UE_LOG(LogBackChannel, Warning, TEXT("Calling code rejected connection on %s"), *Socket->GetDescription());
						BCConnection->Close();
					}
					else
					{
						UE_LOG(LogBackChannel, Log, TEXT("Accepted connection on %s"), *Socket->GetDescription());
					}
				}
			}
		}
	}
	else
	{
		CloseWithError(TEXT("Connection Check Failed"));
	}

	return CheckSucceeded;
}

bool FBackChannelConnection::Attach(FSocket* InSocket)
{
	FScopeLock Lock(&SocketMutex);

	check(Socket == nullptr);

	Socket = InSocket;
	return true;
}


int32 FBackChannelConnection::SendData(const void* InData, const int32 InSize)
{
	FScopeLock Lock(&SocketMutex);
	if (!Socket)
	{
		return -1;
	}

	int32 BytesSent(0);
	Socket->Send((const uint8*)InData, InSize, BytesSent);

	if (BytesSent == -1)
	{
		if (GBackChannelLogErrors)
		{
			ESocketErrors LastError = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLastErrorCode();
			const TCHAR* SocketErr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetSocketError(LastError);
			UE_CLOG(GBackChannelLogErrors, LogBackChannel, Error, TEXT("Failed to send %d bytes of data to %s. Err: %s"), InSize, *GetDescription(), SocketErr);
		}
	}
	else
	{
		INC_DWORD_STAT_BY(STAT_BackChannelBytesSent, BytesSent);

		UE_CLOG(GBackChannelLogPackets, LogBackChannel, Log, TEXT("Sent %d bytes of data"), BytesSent);
	}
	return BytesSent;
}

int32 FBackChannelConnection::ReceiveData(void* OutBuffer, const int32 BufferSize)
{
	FScopeLock Lock(&SocketMutex);
	if (!Socket)
	{
		return 0;
	}

	int32 BytesRead(0);
	Socket->Recv((uint8*)OutBuffer, BufferSize, BytesRead, ESocketReceiveFlags::None);

	// todo - close connection on certain errors
	if (BytesRead > 0)
	{
		INC_DWORD_STAT_BY(STAT_BackChannelBytesRecv, BytesRead);

		PacketsReceived++;
		UE_CLOG(GBackChannelLogPackets, LogBackChannel, Log, TEXT("Received %d bytes of data"), BytesRead);
	}
	return BytesRead;
}
