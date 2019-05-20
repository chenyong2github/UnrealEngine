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
	struct FSession
	{
		FSocket*		Socket;
		IOutDataStream*	StoreSession;
		bool			bDead;
	};

	virtual uint32		Run() override;
	virtual bool		IsRunning() const override;
	virtual bool		StartRecording() override;
	virtual void		StopRecording() override;
	virtual uint32		GetSessionCount() const override;
	TSharedRef<IStore>	Store;
	TArray<FSession>	Sessions;
	FRunnableThread*	Thread = nullptr;
	FSocket*			ListenSocket = nullptr;
	volatile bool		bStopRequested;
};

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
uint32 FRecorder::Run()
{
	ISocketSubsystem& Sockets = *(ISocketSubsystem::Get());

	auto KillSession = [&] (FSession& Session)
	{
		Session.Socket->Close();
		Sockets.DestroySocket(Session.Socket);
		delete Session.StoreSession;
	};

	static const uint32 BufferSize = 5 * 1024 * 1024;
	uint8* Buffer = new uint8[BufferSize];

	while (!bStopRequested)
	{
		bool bPending;
		if (!ListenSocket->HasPendingConnection(bPending))
		{
			break;
		}

		if (bPending)
		{
			FSocket* Socket = ListenSocket->Accept(TEXT("TraceRecClient"));

			bool bAcceptable = false;
			if (Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan(ETimespan::TicksPerSecond / 2)))
			{
				uint32 Magic;
				int32 RecvSize;
				if (Socket->Recv((uint8*)&Magic, sizeof(Magic), RecvSize))
				{
					bAcceptable = (RecvSize == sizeof(Magic)) & (Magic == 'TRCE');
				}
			}

			if (bAcceptable)
			{
				IOutDataStream* StoreSession = Store->CreateNewSession();
				FSession Session = { Socket, StoreSession, false };
				Sessions.Add(Session);
			}
			else
			{
				Sockets.DestroySocket(Socket);
			}
		}

		if (Sessions.Num() == 0)
		{
			FPlatformProcess::SleepNoStats(0.1f);
			continue;
		}

		for (FSession& Session : Sessions)
		{
			uint32 PendingSize;
			FSocket& Socket = *(Session.Socket);
			if (!Socket.HasPendingData(PendingSize))
			{
				if (Socket.GetConnectionState() != SCS_Connected)
				{
					Session.bDead = true;
				}
				continue;
			}

			while (PendingSize)
			{
				int32 RecvSize = (PendingSize > BufferSize) ? BufferSize : PendingSize;
				if (!Socket.Recv(Buffer, RecvSize, RecvSize))
				{
					Session.bDead = true;
					break;
				}

				Session.StoreSession->Write(Buffer, RecvSize);
				PendingSize -= RecvSize;
			}
		}

		// Reap dead sessions
		int SessionCount = Sessions.Num();
		for (int i = 0; i < SessionCount; ++i)
		{
			FSession& Session = Sessions[i];
			if (!Session.bDead)
			{
				continue;
			}

			KillSession(Session);

			--SessionCount;
			Sessions[i] = Sessions[SessionCount];
		}
		Sessions.SetNumUnsafeInternal(SessionCount);
	}

	for (FSession& Session : Sessions)
	{
		KillSession(Session);
	}

	ListenSocket->Close();
	Sockets.DestroySocket(ListenSocket);
	ListenSocket = nullptr;

	delete[] Buffer;
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

	bStopRequested = true;
	Thread->Kill(true);
	delete Thread;
	Thread = nullptr;
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
