// Copyright Epic Games, Inc. All Rights Reserved.

#include "EventFilterService.h"

#include "Modules/ModuleManager.h"
#include "Templates/SharedPointer.h"
#include "Algo/Transform.h"
#include "HAL/PlatformFilemanager.h"

#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/ITraceServicesModule.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/SessionService.h"

#include "Trace/SessionTraceFilterService.h"

#include "IFilterPreset.h"
#include "FilterPresets.h"

FEventFilterService& FEventFilterService::Get()
{
	static FEventFilterService Service;
	return Service;
}

TSharedRef<ISessionTraceFilterService> FEventFilterService::GetFilterServiceByHandle(Trace::FSessionHandle InHandle)
{
	/** Find or add a per-session filter service */
	if (TSharedPtr<ISessionTraceFilterService>* Service = PerHandleFilterService.Find(InHandle))
	{
		return Service->ToSharedRef();
	}
	else
	{
		TSharedPtr<ISessionTraceFilterService> NewService = nullptr;
		for (TSharedPtr<const Trace::IAnalysisSession> Session : AnalysisSessions)
		{
			if (SessionService->GetSessionHandleByName(Session->GetName()) == InHandle)
			{
				NewService = MakeShareable(new FSessionTraceFilterService(InHandle, Session.ToSharedRef()));
			}
		}

		if (NewService == nullptr)
		{
			TSharedPtr<const Trace::IAnalysisSession> Session = SessionService->StartAnalysis(InHandle);

			TSharedPtr<FSessionTraceFilterService> TraceService = MakeShareable(new FSessionTraceFilterService(InHandle, Session.ToSharedRef()));
			NewService = TraceService;
		}

		ensure(NewService != nullptr);
		PerHandleFilterService.Add(InHandle, NewService);
		return NewService->AsShared();
	}
}

FEventFilterService::FEventFilterService()
{
	ITraceServicesModule& TraceServicesModule = FModuleManager::LoadModuleChecked<ITraceServicesModule>("TraceServices");
	TSharedPtr<Trace::IAnalysisService> TraceAnalysisService = TraceServicesModule.GetAnalysisService();

	SessionService = TraceServicesModule.GetSessionService();

	/** Hook into analysis callbacks to track active sessions */
	TraceAnalysisService->OnAnalysisStarted().AddLambda([this](TSharedRef<const Trace::IAnalysisSession> Session)
	{
		AnalysisSessions.Add(Session);
	});

	TraceAnalysisService->OnAnalysisFinished().AddLambda([this](TSharedRef<const Trace::IAnalysisSession> Session)
	{
		AnalysisSessions.Remove(Session);
	});
}
