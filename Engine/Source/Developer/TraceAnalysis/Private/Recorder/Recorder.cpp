// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Trace/Recorder.h"

#include "Containers/Array.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "IPAddress.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Trace/DataStream.h"
#include "Trace/Store.h"

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
	FSession*			AcceptSession(FSocket& Socket);
	void				CloseSession(FSession& Session);
	void				ReapDeadSessions();
	TSharedRef<IStore>	Store;
	TArray<FSession*>	Sessions;
	FRunnableThread*	Thread = nullptr;
	FSocket*			ListenSocket = nullptr;
	volatile bool		bStopRequested;
};



////////////////////////////////////////////////////////////////////////////////
struct FRecorder::FSession
	: public FRunnable
{
	virtual uint32		Run() override;
	FSocket*			Socket;
	IOutDataStream*		StoreSession;
	FRunnableThread*	Thread;
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

		if (!StoreSession->Write(Buffer, RecvSize))
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

	IOutDataStream* StoreSession = Store->CreateNewSession();
	if (!StoreSession)
	{
		return nullptr;
	}
	FSession* Session = new FSession();
	Session->Socket = &Socket;
	Session->StoreSession = StoreSession;
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
	delete Session.StoreSession;
	delete &Session;
}

////////////////////////////////////////////////////////////////////////////////
void FRecorder::ReapDeadSessions()
{
	// Reap dead sessions
	int SessionCount = Sessions.Num();
	for (int i = 0; i < SessionCount; ++i)
	{
		FSession* Session = Sessions[i];
		if (!Session->bDead)
		{
			continue;
		}

		CloseSession(*Session);

		--SessionCount;
		Sessions[i] = Sessions[SessionCount];
	}
	Sessions.SetNumUnsafeInternal(SessionCount);
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
				Sessions.Add(Session);
			}
			else
			{
				Sockets.DestroySocket(Socket);
			}
		}

		ReapDeadSessions();
	}

	for (FSession* Session : Sessions)
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
	return Sessions.Num();
}



////////////////////////////////////////////////////////////////////////////////
TSharedPtr<IRecorder> Recorder_Create(TSharedRef<IStore> Store)
{
	return MakeShared<FRecorder>(Store);
}

} // namespace Trace
