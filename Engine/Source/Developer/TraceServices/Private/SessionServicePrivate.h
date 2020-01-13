// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/SessionService.h"
#include "Misc/ScopeLock.h"
#include "Containers/Ticker.h"
#include "Trace/Recorder.h"
#include "Trace/Store.h"

namespace Trace
{

class FModuleService;
class FAnalysisService;

class FSessionService
	: public ISessionService
{
public:
	FSessionService(FModuleService& ModuleService, FAnalysisService& AnalysisService);
	FSessionService(FModuleService& ModuleService, FAnalysisService& AnalysisService, const TCHAR* OverrideSessionsDirectory);
	virtual ~FSessionService();
	virtual bool StartRecorderServer() override;
	virtual bool IsRecorderServerRunning() const override;
	virtual void StopRecorderServer() override;
	virtual const TCHAR* GetLocalSessionDirectory() const override { return *LocalSessionDirectory; }
	virtual void GetAvailableSessions(TArray<Trace::FSessionHandle>& OutSessions) const override;
	virtual void GetLiveSessions(TArray<Trace::FSessionHandle>& OutSessions) const override;
	virtual bool GetSessionInfo(Trace::FSessionHandle SessionHandle, Trace::FSessionInfo& OutSessionInfo) const override;
	virtual void SetModuleEnabled(Trace::FSessionHandle SessionHandle, const FName& ModuleName, bool bState) override;
	virtual bool IsModuleEnabled(Trace::FSessionHandle SessionHandle, const FName& ModuleName) const override;
	virtual bool ToggleChannels(Trace::FSessionHandle SessionHandle, const TCHAR* Channels, bool bState) override;
	virtual bool ConnectSession(const TCHAR* ControlClientAddress) override;
	virtual TSharedPtr<const IAnalysisSession> StartAnalysis(FSessionHandle SessionHandle) override;

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
	FAnalysisService& AnalysisService;
	void* RecorderEvent = nullptr;
	FString LocalSessionDirectory;
	TSharedPtr<IStore> TraceStore;
	TSharedPtr<IRecorder> TraceRecorder;
	mutable FCriticalSection SessionsCS;
	TMap<FStoreSessionHandle, FSessionInfoInternal> Sessions;
	FDelegateHandle TickHandle;
};

}
