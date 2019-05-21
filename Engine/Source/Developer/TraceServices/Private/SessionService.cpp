// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TraceServices/SessionService.h"
#include "SessionServicePrivate.h"
#include "Trace/Store.h"
#include "Trace/Recorder.h"
#include "Trace/DataStream.h"
#include "Misc/Paths.h"

namespace Trace
{

FSessionService::FSessionService()
{
	LocalSessionDirectory = FPaths::ProjectSavedDir() / TEXT("TraceSessions");
	TraceStore = Store_Create(*LocalSessionDirectory);
	TraceRecorder = Recorder_Create(TraceStore.ToSharedRef());
}

FSessionService::~FSessionService()
{
	
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

}
