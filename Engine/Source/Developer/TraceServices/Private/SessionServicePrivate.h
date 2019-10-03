// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/SessionService.h"
#include "Misc/ScopeLock.h"
#include "Containers/Ticker.h"
#include "Trace/Recorder.h"
#include "Trace/Store.h"
#include "ModuleServicePrivate.h"

namespace Trace
{

class FSessionService
	: public ISessionService
{
public:
	FSessionService(FModuleService& ModuleService);
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
	virtual void SetModuleEnabled(Trace::FSessionHandle SessionHandle, const FName& ModuleName, bool bState) override;
	virtual bool IsModuleEnabled(Trace::FSessionHandle SessionHandle, const FName& ModuleName) const override;
	virtual bool ConnectSession(const TCHAR* ControlClientAddress) override;

private:
	struct FSessionInfoInternal
	{
		FRecorderSessionHandle RecorderSessionHandle;
		const TCHAR* Uri;
		const TCHAR* Name;
		FString Platform;
		FString AppName;
		FString CommandLine;
		TMap<FName, TSet<FString>> EnabledModuleLoggersMap;
		FDateTime TimeStamp;
		uint64 Size;
		bool bIsLive;
		int8 ConfigurationType;
		int8 TargetType;
		bool bIsUpdated = false;
	};

	bool Tick(float DeltaTime);
	void UpdateSessions();
	void UpdateSessionContext(FStoreSessionHandle StoreHandle, FSessionInfoInternal& Info);

	FModuleService& ModuleService;
	void* RecorderEvent = nullptr;
	FString LocalSessionDirectory;
	TSharedPtr<IStore> TraceStore;
	TSharedPtr<IRecorder> TraceRecorder;
	mutable FCriticalSection SessionsCS;
	TMap<FStoreSessionHandle, FSessionInfoInternal> Sessions;
	FDelegateHandle TickHandle;
};

}
