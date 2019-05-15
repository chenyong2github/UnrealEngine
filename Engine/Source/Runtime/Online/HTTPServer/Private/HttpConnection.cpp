// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "HttpConnection.h"
#include "HttpConnectionContext.h"
#include "HttpConnectionRequestReadContext.h"
#include "HttpConnectionResponseWriteContext.h"
#include "HttpRouter.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "HttpRequestHandlerIterator.h"
#include "HttpServerConstants.h"
#include "HttpServerConstantsPrivate.h"

#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Containers/Array.h"

DEFINE_LOG_CATEGORY(LogHttpConnection)

FHttpConnection::FHttpConnection(FSocket* InSocket, TSharedPtr<FHttpRouter> InRouter)
	: Socket(InSocket)
	,Router(InRouter)
	,ReadContext(InSocket)
	,WriteContext(InSocket)
{
	check(nullptr != Socket);
}

FHttpConnection::~FHttpConnection()
{
	check(nullptr == Socket);
}

void FHttpConnection::Tick(float DeltaTime)
{
	const float AwaitReadTimeout = bKeepAlive ?
		ConnectionKeepAliveTimeout : ConnectionTimeout;

	switch (State)
	{
	case EHttpConnectionState::AwaitingRead:
		if (ReadContext.GetElapsedIdleTime() > AwaitReadTimeout)
		{
			Destroy();
			return;
		}
		BeginRead(DeltaTime);
		break;

	case EHttpConnectionState::Reading:
		if (ReadContext.GetElapsedIdleTime() > ConnectionTimeout)
		{
			Destroy();
			return;
		}
		ContinueRead(DeltaTime);
		break;

	case EHttpConnectionState::AwaitingProcessing:
		break;

	case EHttpConnectionState::Writing:
		if (WriteContext.GetElapsedIdleTime() > ConnectionTimeout)
		{
			Destroy();
			return;
		}
		ContinueWrite(DeltaTime);
		break;

	case EHttpConnectionState::Destroyed:
		ensure(false);
		break;
	}
}

void FHttpConnection::ChangeState(EHttpConnectionState NewState)
{
	check(EHttpConnectionState::Destroyed != State);
	check(NewState != State);

	UE_LOG(LogHttpConnection, Verbose,
		TEXT("ChangingState: %d => %d"), State, NewState);
	State = NewState;
}

void FHttpConnection::TransferState(EHttpConnectionState CurrentState, EHttpConnectionState NextState)
{
	check(CurrentState == State);
	check(NextState != CurrentState);
	ChangeState(NextState);
}

void FHttpConnection::BeginRead(float DeltaTime)
{
	// Wait should always return true if the connection is valid
	if (!Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::Zero()))
	{
		Destroy();
		return;
	}

	// The socket is reachable, however there may not be data in the pipe
	uint32 PendingDataSize = 0;
	if (Socket->HasPendingData(PendingDataSize))
	{
		TransferState(EHttpConnectionState::AwaitingRead, EHttpConnectionState::Reading);
		ReadContext.ResetContext();
		ContinueRead(DeltaTime);
	}
	else
	{
		ReadContext.AddElapsedIdleTime(DeltaTime);
	}
}

void FHttpConnection::ContinueRead(float DeltaTime)
{
	check(State == EHttpConnectionState::Reading);

	auto ReaderState = ReadContext.ReadStream(DeltaTime);

	switch (ReaderState)
	{
	case EHttpConnectionContextState::Continue:
		break;

	case EHttpConnectionContextState::Done:
		CompleteRead(ReadContext.GetRequest());
		break;

	case EHttpConnectionContextState::Error:
		HandleReadError(*ReadContext.GetErrorStr());
		break;
	}
}

void FHttpConnection::CompleteRead(const TSharedPtr<FHttpServerRequest>& Request)
{
	TArray<FString>* ConnectionHeaders = Request->Headers.Find(FHttpServerHeaderKeys::CONNECTION);
	if (ConnectionHeaders &&
		ConnectionHeaders->Contains(TEXT("Keep-Alive")))
	{
		bKeepAlive = true;
	}

	TWeakPtr<FHttpConnection> WeakThisPtr(AsShared());
	FHttpResultCallback OnProcessingComplete = [WeakThisPtr, Request, LastRequestNumberStateCapture = ++LastRequestNumber]
	(TUniquePtr<FHttpServerResponse>&& Response)
	{
		TSharedPtr<FHttpConnection> SharedThisPtr = WeakThisPtr.Pin();
		if (SharedThisPtr.IsValid())
		{
			UE_LOG(LogHttpConnection, Log,
				TEXT("Completed Processing Request [%u]"), LastRequestNumberStateCapture)

			// Ensure this result callback was called once
			check(EHttpConnectionState::AwaitingProcessing == SharedThisPtr->GetState());

			// Begin response flow
			SharedThisPtr->BeginWrite(MoveTemp(Response), LastRequestNumberStateCapture);
		}
	};

	ProcessRequest(Request, OnProcessingComplete);
}

void FHttpConnection::ProcessRequest(const TSharedPtr<FHttpServerRequest>& Request, const FHttpResultCallback& OnProcessingComplete)
{
	TransferState(EHttpConnectionState::Reading, EHttpConnectionState::AwaitingProcessing);

	UE_LOG(LogHttpConnection, Log,
		TEXT("Begin Processing Request [%u]: %s"), LastRequestNumber, *Request->RelativePath.GetPath());

	bool bRequestHandled = false;
	auto RequestHandlerIterator = Router->CreateRequestHandlerIterator(Request);
	while (const auto& RequestHandlerPtr = RequestHandlerIterator.Next())
	{
		(*RequestHandlerPtr).CheckCallable();
		bRequestHandled = (*RequestHandlerPtr)(*Request, OnProcessingComplete);
		if (bRequestHandled)
		{
			break;
		}
		// If this handler didn't accept, ensure they did not invoke the result callback
		check(State == EHttpConnectionState::AwaitingProcessing);
	}

	if (!bRequestHandled)
	{
		const FString& ResponseError(FHttpServerErrorStrings::NotFound);
		auto Response = FHttpServerResponse::Create(EHttpServerResponseCodes::NotFound, ResponseError);
		OnProcessingComplete(MoveTemp(Response));
	}
}

void FHttpConnection::BeginWrite(TUniquePtr<FHttpServerResponse>&& Response, uint32 RequestNumber)
{
	// Ensure the passed-in request number is the one we expect
	check(RequestNumber == LastRequestNumber);

	ChangeState(EHttpConnectionState::Writing);

	if (bKeepAlive)
	{
		const FString& KeepAliveStr = FString::Printf(TEXT("timeout=%f"), ConnectionKeepAliveTimeout);
		const TArray<FString>& KeepAliveValue = { KeepAliveStr };
		Response->Headers.Add(FHttpServerHeaderKeys::KEEP_ALIVE, KeepAliveValue);
	}

	WriteContext.ResetContext(MoveTemp(Response));
	ContinueWrite(0.0f);
}

void FHttpConnection::ContinueWrite(float DeltaTime)
{
	check(State == EHttpConnectionState::Writing);

	auto WriterState = WriteContext.WriteStream(DeltaTime);
	switch (WriterState)
	{
	case EHttpConnectionContextState::Continue:
		break;

	case EHttpConnectionContextState::Done:
		CompleteWrite();
		break;

	case EHttpConnectionContextState::Error:
		HandleWriteError(*WriteContext.GetErrorStr());
		break;
	}
}

void FHttpConnection::CompleteWrite()
{
	check(EHttpConnectionState::Writing == State);

	if (bKeepAlive && !bGracefulDestroyRequested)
	{
		ChangeState(EHttpConnectionState::AwaitingRead);
	}
	else
	{
		Destroy();
	}
}

void FHttpConnection::RequestDestroy(bool bGraceful)
{
	if (EHttpConnectionState::Destroyed == State)
	{
		return;
	}

	bGracefulDestroyRequested = bGraceful;

	// If we aren't gracefully destroying, or we are otherwise already 
	// awaiting a read operation (not started yet), destroy() immediately.
	if (!bGracefulDestroyRequested || State == EHttpConnectionState::AwaitingRead)
	{
		Destroy();
	}
}

void FHttpConnection::Destroy()
{
	check(State != EHttpConnectionState::Destroyed);
	ChangeState(EHttpConnectionState::Destroyed);

	if (Socket)
	{
		ISocketSubsystem *SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (SocketSubsystem)
		{
			SocketSubsystem->DestroySocket(Socket);
		}
		Socket = nullptr;
	}
}

void FHttpConnection::HandleReadError(const TCHAR* ErrorCode)
{
	UE_LOG(LogHttpConnection, Error, TEXT("%s"), ErrorCode);

	// Forcibly Reply
	bKeepAlive = false;
	auto ResponseCode = EHttpServerResponseCodes::BadRequest;
	auto Response = FHttpServerResponse::Create(ResponseCode, ErrorCode);

	BeginWrite(MoveTemp(Response), ++LastRequestNumber);
}

void FHttpConnection::HandleWriteError(const TCHAR* ErrorCode)
{
	UE_LOG(LogHttpConnection, Error, TEXT("%s"), ErrorCode);

	// Forcibly Close
	bKeepAlive = false;
	Destroy();
}