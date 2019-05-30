// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TraceServices/SessionService.h"
#include "SessionServicePrivate.h"
#include "Trace/DataStream.h"
#include "Misc/Paths.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Trace/ControlClient.h"

namespace Trace
{

FSessionService::FSessionService()
{
	LocalSessionDirectory = FPaths::ProjectSavedDir() / TEXT("TraceSessions");
	TraceStore = Store_Create(*LocalSessionDirectory);
	TraceRecorder = Recorder_Create(TraceStore.ToSharedRef());

	FTickerDelegate TickDelegate = FTickerDelegate::CreateRaw(this, &FSessionService::Tick);
	TickHandle = FTicker::GetCoreTicker().AddTicker(TickDelegate, 0.5f);
}

FSessionService::~FSessionService()
{
	if (TickHandle.IsValid())
	{
		FTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
}

bool FSessionService::StartRecorderServer()
{
	return TraceRecorder->StartRecording();
}

bool FSessionService::IsRecorderServerRunning() const
{
	return (TraceRecorder == nullptr) ? false : TraceRecorder->IsRunning();
}

void FSessionService::StopRecorderServer()
{
	TraceRecorder->StopRecording();
}

void FSessionService::GetAvailableSessions(TArray<FSessionHandle>& OutSessions) const
{
	FScopeLock Lock(&SessionsCS);
	OutSessions.Reserve(OutSessions.Num() + Sessions.Num());
	for (const auto& KV : Sessions)
	{
		OutSessions.Add(static_cast<FSessionHandle>(KV.Key));
	}
}

void FSessionService::GetLiveSessions(TArray<FSessionHandle>& OutSessions) const
{
	FScopeLock Lock(&SessionsCS);
	OutSessions.Reserve(OutSessions.Num() + Sessions.Num());
	for (const auto& KV : Sessions)
	{
		if (KV.Value.bIsLive)
		{
			OutSessions.Add(static_cast<FSessionHandle>(KV.Key));
		}
	}
}

bool FSessionService::GetSessionInfo(FSessionHandle SessionHandle, FSessionInfo& OutSessionInfo) const
{
	FScopeLock Lock(&SessionsCS);
	const FSessionInfoInternal* FindIt = Sessions.Find(SessionHandle);
	if (!FindIt)
	{
		return false;
	}
	OutSessionInfo.Uri = FindIt->Uri;
	OutSessionInfo.Name = FindIt->Name;
	OutSessionInfo.bIsLive = FindIt->bIsLive;
	return true;
}

Trace::IInDataStream* FSessionService::OpenSessionStream(FSessionHandle SessionHandle)
{
	return TraceStore->OpenSessionStream(SessionHandle);
}

Trace::IInDataStream* FSessionService::OpenSessionFromFile(const TCHAR* FilePath)
{
	return Trace::DataStream_ReadFile(FilePath);
}

bool FSessionService::Tick(float DeltaTime)
{
	UpdateSessions();
	return true;
}

void FSessionService::UpdateSessions()
{
	TArray<FStoreSessionInfo> StoreSessions;
	TraceStore->GetAvailableSessions(StoreSessions);
	TArray<FRecorderSessionInfo> RecorderSessions;
	TraceRecorder->GetActiveSessions(RecorderSessions);

	FScopeLock Lock(&SessionsCS);
	for (const FStoreSessionInfo& StoreSession : StoreSessions)
	{
		FSessionInfoInternal& Session = Sessions.FindOrAdd(StoreSession.Handle);
		Session.Uri = StoreSession.Uri;
		Session.Name = StoreSession.Name;
		Session.bIsLive = StoreSession.bIsLive;
		Session.RecorderSessionHandle = 0;
	}

	for (const FRecorderSessionInfo& RecorderSession : RecorderSessions)
	{
		FSessionInfoInternal* FindIt = Sessions.Find(RecorderSession.StoreSessionHandle);
		if (FindIt)
		{
			FindIt->RecorderSessionHandle = RecorderSession.Handle;
		}
	}
}

}
