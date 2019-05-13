// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TraceServices/SessionService.h"
#include "SessionServicePrivate.h"
#include "Trace/Store.h"
#include "Trace/Recorder.h"
#include "Trace/DataStream.h"

namespace Trace
{

FSessionService::FSessionService(TSharedRef<IStore> InTraceStore)
	: TraceStore(InTraceStore)
{
	
}

FSessionService::~FSessionService()
{
	
}

void FSessionService::StartRecorderServer()
{
	Trace::Recorder_StartServer(TraceStore);
}

void FSessionService::GetAvailableSessions(TArray<Trace::FSessionHandle>& OutSessions)
{
	TraceStore->GetAvailableSessions(OutSessions);
}

bool FSessionService::GetSessionInfo(Trace::FSessionHandle SessionHandle, Trace::FSessionInfo& OutSessionInfo)
{
	return TraceStore->GetSessionInfo(SessionHandle, OutSessionInfo);
}

Trace::IInDataStream* FSessionService::OpenSessionStream(Trace::FSessionHandle SessionHandle)
{
	return TraceStore->OpenSessionStream(SessionHandle);
}

Trace::IInDataStream* FSessionService::OpenSessionFromFile(const TCHAR* FilePath)
{
	return Trace::DataStream_ReadFile(FilePath);
}

/*void FSessionService::GetSessions(TArray<TSharedRef<FSessionInfo>>& OutSessions)
{
	OutSessions.Reset(AvailableSessions.Num());
	for (const auto& KV : AvailableSessions)
	{
		OutSessions.Add(KV.Value);
	}
}*/

/*void FSessionService::SelectSession(TSharedPtr<FSessionInfo> Session)
{
	SelectedSession = Session;
	OnSessionSelected().Broadcast(SelectedSession);
}*/

}