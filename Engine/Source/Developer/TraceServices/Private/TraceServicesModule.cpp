// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "TraceServices/ITraceServicesModule.h"
#include "SessionServicePrivate.h"
#include "AnalysisServicePrivate.h"

class FTraceServicesModule
	: public ITraceServicesModule
{
public:
	virtual void StartupModule() override;
	virtual TSharedPtr<Trace::ISessionService> GetSessionService() override;
	virtual TSharedPtr<Trace::IAnalysisService> GetAnalysisService() override;

private:
	TSharedPtr<Trace::IStore> TraceStore;
	TSharedPtr<Trace::FSessionService> SessionService;
	TSharedPtr<Trace::FAnalysisService> AnalysisService;
};

void FTraceServicesModule::StartupModule()
{
	TraceStore = Trace::Store_Create(TEXT("D:\\Trace"));
}

TSharedPtr<Trace::ISessionService> FTraceServicesModule::GetSessionService()
{
	if (!SessionService.IsValid())
	{
		SessionService = MakeShareable(new Trace::FSessionService(TraceStore.ToSharedRef()));
	}
	return SessionService;
}

TSharedPtr<Trace::IAnalysisService> FTraceServicesModule::GetAnalysisService()
{
	if (!AnalysisService.IsValid())
	{
		AnalysisService = MakeShareable(new Trace::FAnalysisService(TraceStore.ToSharedRef()));
	}
	return AnalysisService;
}

IMPLEMENT_MODULE(FTraceServicesModule, TraceServices)
