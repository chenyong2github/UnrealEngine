// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "TraceServices/ITraceServicesModule.h"
#include "SessionServicePrivate.h"
#include "AnalysisServicePrivate.h"
#include "ModuleServicePrivate.h"
#include "Features/IModularFeatures.h"
#include "Modules/TimingProfilerModule.h"
#include "Modules/LoadTimeProfilerModule.h"
#include "Modules/StatsModule.h"
#include "Stats/StatsTrace.h"

class FTraceServicesModule
	: public ITraceServicesModule
{
public:
	virtual TSharedPtr<Trace::ISessionService> GetSessionService() override;
	virtual TSharedPtr<Trace::IAnalysisService> GetAnalysisService() override;
	virtual TSharedPtr<Trace::IModuleService> GetModuleService() override;

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TSharedPtr<Trace::FSessionService> SessionService;
	TSharedPtr<Trace::FAnalysisService> AnalysisService;
	TSharedPtr<Trace::FModuleService> ModuleService;

	Trace::FTimingProfilerModule TimingProfilerModule;
	Trace::FLoadTimeProfilerModule LoadTimeProfilerModule;
	Trace::FStatsModule StatsModule;
};

TSharedPtr<Trace::ISessionService> FTraceServicesModule::GetSessionService()
{
	if (!SessionService.IsValid())
	{
		GetModuleService();
		SessionService = MakeShared<Trace::FSessionService>(*ModuleService.Get());
	}
	return SessionService;
}

TSharedPtr<Trace::IAnalysisService> FTraceServicesModule::GetAnalysisService()
{
	if (!AnalysisService.IsValid())
	{
		GetModuleService();
		AnalysisService = MakeShared<Trace::FAnalysisService>(*ModuleService.Get());
	}
	return AnalysisService;
}

TSharedPtr<Trace::IModuleService> FTraceServicesModule::GetModuleService()
{
	if (!ModuleService.IsValid())
	{
		ModuleService = MakeShared<Trace::FModuleService>();
	}
	return ModuleService;
}

void FTraceServicesModule::StartupModule()
{
	IModularFeatures::Get().RegisterModularFeature(Trace::ModuleFeatureName, &TimingProfilerModule);
	IModularFeatures::Get().RegisterModularFeature(Trace::ModuleFeatureName, &LoadTimeProfilerModule);
#if EXPERIMENTAL_STATSTRACE_ENABLED
	IModularFeatures::Get().RegisterModularFeature(Trace::ModuleFeatureName, &StatsModule);
#endif
}

void FTraceServicesModule::ShutdownModule()
{
#if EXPERIMENTAL_STATSTRACE_ENABLED
	IModularFeatures::Get().UnregisterModularFeature(Trace::ModuleFeatureName, &StatsModule);
#endif
	IModularFeatures::Get().UnregisterModularFeature(Trace::ModuleFeatureName, &LoadTimeProfilerModule);
	IModularFeatures::Get().UnregisterModularFeature(Trace::ModuleFeatureName, &TimingProfilerModule);
}

IMPLEMENT_MODULE(FTraceServicesModule, TraceServices)
