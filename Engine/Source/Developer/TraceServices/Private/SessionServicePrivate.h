// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/SessionService.h"

namespace Trace
{

class FSessionService
	: public ISessionService
{
public:
	FSessionService(TSharedRef<IStore> InTraceStore);
	virtual ~FSessionService();
	virtual void StartRecorderServer() override;
	virtual void GetAvailableSessions(TArray<Trace::FSessionHandle>& OutSessions) override;
	virtual bool GetSessionInfo(Trace::FSessionHandle SessionHandle, Trace::FSessionInfo& OutSessionInfo) override;
	virtual Trace::IInDataStream* OpenSessionStream(Trace::FSessionHandle SessionHandle) override;
	virtual Trace::IInDataStream* OpenSessionFromFile(const TCHAR* FilePath) override;

private:
	TSharedRef<IStore> TraceStore;
};

}