// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/SessionService.h"
#include "Misc/ScopeLock.h"
#include "Containers/Ticker.h"
#include "Trace/Recorder.h"
#include "Trace/Store.h"

namespace Trace
{

class FSessionService
	: public ISessionService
{
public:
	FSessionService();
	virtual ~FSessionService();
	virtual bool StartRecorderServer() override;
	virtual bool IsRecorderServerRunning() const override;
	virtual void StopRecorderServer() override;
	virtual const TCHAR* GetLocalSessionDirectory() const override { return *LocalSessionDirectory; }
	virtual void GetAvailableSessions(TArray<Trace::FSessionHandle>& OutSessions) const override;
	virtual void GetLiveSessions(TArray<Trace::FSessionHandle>& OutSessions) const override;
	virtual bool GetSessionInfo(Trace::FSessionHandle SessionHandle, Trace::FSessionInfo& OutSessionInfo) const override;
	virtual Trace::IInDataStream* OpenSessionStream(Trace::FSessionHandle SessionHandle) override;
	virtual Trace::IInDataStream* OpenSessionFromFile(const TCHAR* FilePath) override;

private:
	bool Tick(float DeltaTime);
	void UpdateSessions();

	struct FSessionInfoInternal
	{
		FRecorderSessionHandle RecorderSessionHandle;
		const TCHAR* Uri;
		const TCHAR* Name;
		bool bIsLive;
	};

	FString LocalSessionDirectory;
	TSharedPtr<IStore> TraceStore;
	TSharedPtr<IRecorder> TraceRecorder;
	mutable FCriticalSection SessionsCS;
	TMap<FStoreSessionHandle, FSessionInfoInternal> Sessions;
	FDelegateHandle TickHandle;
};

}
