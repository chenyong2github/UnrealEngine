// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Trace/Recorder.h"

#include "Containers/Array.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "IPAddress.h"
#include "Misc/ScopeLock.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Trace/DataStream.h"
#include "Trace/Store.h"
#include "Trace/ControlClient.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
class FRecorder
	: public IRecorder
	, public FRunnable
{
public:
						FRecorder(TSharedRef<IStore> InStore);
	virtual				~FRecorder();

private:
	struct				FSession;
	virtual uint32		Run() override;
	virtual bool		IsRunning() const override;
	virtual bool		StartRecording() override;
	virtual void		StopRecording() override;
	virtual uint32		GetSessionCount() const override;
	virtual void		GetActiveSessions(TArray<FRecorderSessionInfo>& OutSessions) const override;
	virtual bool		ToggleEvent(FRecorderSessionHandle RecordingHandle, const TCHAR* LoggerWildcard, bool bState) override;
	FSession*			AcceptSession(FSocket& Socket);
	void				CloseSession(FSession& Session);
	void				ReapDeadSessions();
	TSharedRef<IStore>	Store;
	mutable FCriticalSection SessionsCS;
	TMap<FRecorderSessionHandle, FSession*> Sessions;
	FRunnableThread*	Thread = nullptr;
	FSocket*			ListenSocket = nullptr;
	FRecorderSessionHandle	NextSessionHandle = 1;
	volatile bool		bStopRequested;
};



////////////////////////////////////////////////////////////////////////////////
struct FRecorder::FSession
	: public FRunnable
{
	virtual uint32		Run() override;
	FSocket*			Socket;
	FStoreSessionHandle	StoreSessionHandle;
	IOutDataStream*		StoreSessionStream;
	FRunnableThread*	Thread;
	TSharedPtr<FInternetAddr> ControlClientAddress;
	volatile bool		bDead = false;
};

////////////////////////////////////////////////////////////////////////////////
uint32 FRecorder::FSession::Run()
{
	static const uint32 BufferSize = 1 * 1024 * 1024;
	uint8* Buffer = new uint8[BufferSize];

	do
	{
		int32 RecvSize;
		if (!Socket->Recv(Buffer, BufferSize, RecvSize))
		{
			bDead = true;
			break;
		}

		if (!StoreSessionStream->Write(Buffer, RecvSize))
		{
			bDead = true;
			break;
		}
	}
	while (!bDead);

	delete[] Buffer;
	return 0;
}



////////////////////////////////////////////////////////////////////////////////
FRecorder::FRecorder(TSharedRef<IStore> InStore)
: Store(InStore)
{
}

////////////////////////////////////////////////////////////////////////////////
FRecorder::~FRecorder()
{
	StopRecording();
}

////////////////////////////////////////////////////////////////////////////////
FRecorder::FSession* FRecorder::AcceptSession(FSocket& Socket)
{
	bool bAcceptable = false;
	if (Socket.Wait(ESocketWaitConditions::WaitForRead, FTimespan(ETimespan::TicksPerSecond / 3)))
	{
		uint32 Magic;
		int32 RecvSize;
		if (Socket.Recv((uint8*)&Magic, sizeof(Magic), RecvSize))
		{
			bAcceptable = (RecvSize == sizeof(Magic)) & (Magic == 'TRCE');
		}
	}

	if (!bAcceptable)
	{
		return nullptr;
	}

	TTuple<FStoreSessionHandle, IOutDataStream*> StoreSession = Store->CreateNewSession();
	if (!StoreSession.Get<1>())
	{
		return nullptr;
	}
	FSession* Session = new FSession();
	Session->ControlClientAddress = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
	Socket.GetPeerAddress(*Session->ControlClientAddress);
	Session->ControlClientAddress->SetPort(1985);
	Session->Socket = &Socket;
	Session->StoreSessionHandle = StoreSession.Get<0>();
	Session->StoreSessionStream = StoreSession.Get<1>();
	Session->Thread = FRunnableThread::Create(Session, TEXT("TraceRecSession"));
	return Session;
}

////////////////////////////////////////////////////////////////////////////////
void FRecorder::CloseSession(FSession& Session)
{
	Session.bDead = true;
	Session.Socket->Close();

	Session.Thread->Kill(true);

	ISocketSubsystem& Sockets = *(ISocketSubsystem::Get());
	Sockets.DestroySocket(Session.Socket);

	delete Session.Thread;
	delete Session.StoreSessionStream;
	delete &Session;
}

////////////////////////////////////////////////////////////////////////////////
void FRecorder::ReapDeadSessions()
{
	// Reap dead sessions
	TArray<FSession*> SessionsToClose;
	{
		FScopeLock Lock(&SessionsCS);
		for (auto It = Sessions.CreateIterator(); It; ++It)
		{
			FSession* Session = It->Value;
			if (Session->bDead)
			{
				SessionsToClose.Add(Session);
				It.RemoveCurrent();
			}
		}
	}
	
	for (FSession* Session : SessionsToClose)
	{
		CloseSession(*Session);
	}
}

////////////////////////////////////////////////////////////////////////////////
uint32 FRecorder::Run()
{
	ISocketSubsystem& Sockets = *(ISocketSubsystem::Get());

	FTimespan WaitTimespan = FTimespan((ETimespan::TicksPerSecond * 3) / 7);
	while (!bStopRequested)
	{
		bool bPending;
		if (!ListenSocket->WaitForPendingConnection(bPending, WaitTimespan))
		{
			break;
		}

		if (bPending)
		{
			FSocket* Socket = ListenSocket->Accept(TEXT("TraceRecClient"));
			if (Socket == nullptr)
			{
				break;
			}

			if (FSession* Session = AcceptSession(*Socket))
			{
				FScopeLock Lock(&SessionsCS);
				Sessions.Add(NextSessionHandle++, Session);
			}
			else
			{
				Sockets.DestroySocket(Socket);
			}
		}

		ReapDeadSessions();
	}

	TArray<FSession*> SessionsToClose;
	{
		FScopeLock Lock(&SessionsCS);
		for (auto& KV : Sessions)
		{
			SessionsToClose.Add(KV.Value);
		}
		Sessions.Empty();
	}
	for (FSession* Session : SessionsToClose)
	{
		CloseSession(*Session);
	}

	return 0;
}

////////////////////////////////////////////////////////////////////////////////
bool FRecorder::IsRunning() const
{
	return (ListenSocket != nullptr);
}

////////////////////////////////////////////////////////////////////////////////
bool FRecorder::StartRecording()
{
	if (IsRunning())
	{
		return true;
	}

	StopRecording();

	ISocketSubsystem& Sockets = *(ISocketSubsystem::Get());

	// Create a socket to use for listening for inbound connections
	FSocket* Socket = Sockets.CreateSocket(NAME_Stream, TEXT("TraceRecListen"));
	if (Socket == nullptr)
	{
		return false;
	}

	// Bind it to the trace-recording port
	bool bBound = false;
	TSharedPtr<FInternetAddr> Addr = Sockets.CreateInternetAddr();
	Addr->SetIp(0);
	Addr->SetPort(1980);
	if (Socket->Bind(*Addr))
	{
		bBound = true;
	}

	// Set the socket listening for connections to accept
	if (!bBound || !Socket->Listen(32))
	{
		Sockets.DestroySocket(Socket);
		return false;
	}

	bStopRequested = false;
	ListenSocket = Socket;
	Thread = FRunnableThread::Create(this, TEXT("TraceRec"));
	return true;
}

////////////////////////////////////////////////////////////////////////////////
void FRecorder::StopRecording()
{
	if (Thread == nullptr || bStopRequested)
	{
		return;
	}

	ListenSocket->Close();

	bStopRequested = true;

	Thread->Kill(true);
	delete Thread;
	Thread = nullptr;

	ISocketSubsystem& Sockets = *(ISocketSubsystem::Get());
	Sockets.DestroySocket(ListenSocket);
	ListenSocket = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
uint32 FRecorder::GetSessionCount() const
{
	FScopeLock Lock(&SessionsCS);
	return Sessions.Num();
}


////////////////////////////////////////////////////////////////////////////////
void FRecorder::GetActiveSessions(TArray<FRecorderSessionInfo>& OutSessions) const
{
	FScopeLock Lock(&SessionsCS);
	OutSessions.Reserve(OutSessions.Num() + Sessions.Num());
	for (const auto& KV : Sessions)
	{
		FRecorderSessionInfo& SessionInfo = OutSessions.AddDefaulted_GetRef();
		SessionInfo.Handle = KV.Key;
		SessionInfo.StoreSessionHandle = KV.Value->StoreSessionHandle;
	}
}

////////////////////////////////////////////////////////////////////////////////
bool FRecorder::ToggleEvent(FRecorderSessionHandle SessionHandle, const TCHAR* LoggerWildcard, bool bState)
{
	TSharedPtr<FInternetAddr> ControlClientAddress;
	{
		FScopeLock Lock(&SessionsCS);
		FSession** FindIt = Sessions.Find(SessionHandle);
		if (!FindIt)
		{
			return false;
		}
		ControlClientAddress = (*FindIt)->ControlClientAddress;
	}
	FControlClient ControlClient;
	if (!ControlClient.Connect(*ControlClientAddress))
	{
		return false;
	}
	ControlClient.SendToggleEvent(LoggerWildcard, bState);
	ControlClient.Disconnect();
	return true;
}

////////////////////////////////////////////////////////////////////////////////
TSharedPtr<IRecorder> Recorder_Create(TSharedRef<IStore> Store)
{
	return MakeShared<FRecorder>(Store);
}

} // namespace Trace
