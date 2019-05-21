// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "TraceServices/ITraceServicesModule.h"
#include "SessionServicePrivate.h"
#include "AnalysisServicePrivate.h"

class FTraceServicesModule
	: public ITraceServicesModule
{
public:
	virtual TSharedPtr<Trace::ISessionService> GetSessionService() override;
	virtual TSharedPtr<Trace::IAnalysisService> GetAnalysisService() override;

private:
	TSharedPtr<Trace::FSessionService> SessionService;
	TSharedPtr<Trace::FAnalysisService> AnalysisService;
};

TSharedPtr<Trace::ISessionService> FTraceServicesModule::GetSessionService()
{
	if (!SessionService.IsValid())
	{
		SessionService = MakeShared<Trace::FSessionService>();
	}
	return SessionService;
}

TSharedPtr<Trace::IAnalysisService> FTraceServicesModule::GetAnalysisService()
{
	if (!AnalysisService.IsValid())
	{
		AnalysisService = MakeShared<Trace::FAnalysisService>();
	}
	return AnalysisService;
}

IMPLEMENT_MODULE(FTraceServicesModule, TraceServices)
