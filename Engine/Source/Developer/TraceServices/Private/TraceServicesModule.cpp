// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "TraceServices/ITraceServicesModule.h"
#include "AnalysisServicePrivate.h"
#include "ModuleServicePrivate.h"
#include "Features/IModularFeatures.h"
#include "Modules/TimingProfilerModule.h"
#include "Modules/LoadTimeProfilerModule.h"
#include "Modules/StatsModule.h"
#include "Modules/CsvProfilerModule.h"
#include "Modules/CountersModule.h"
#include "Modules/NetProfilerModule.h"
#include "Modules/MemoryModule.h"
#include "Modules/DiagnosticsModule.h"

class FTraceServicesModule
	: public ITraceServicesModule
{
public:
	virtual TSharedPtr<Trace::IAnalysisService> GetAnalysisService() override;
	virtual TSharedPtr<Trace::IModuleService> GetModuleService() override;
	virtual TSharedPtr<Trace::IAnalysisService> CreateAnalysisService() override;
	virtual TSharedPtr<Trace::IModuleService> CreateModuleService() override;

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TSharedPtr<Trace::FAnalysisService> AnalysisService;
	TSharedPtr<Trace::FModuleService> ModuleService;

	Trace::FTimingProfilerModule TimingProfilerModule;
	Trace::FLoadTimeProfilerModule LoadTimeProfilerModule;
	Trace::FStatsModule StatsModule;
	Trace::FCsvProfilerModule CsvProfilerModule;
	Trace::FCountersModule CountersModule;
	Trace::FNetProfilerModule NetProfilerModule;
	Trace::FMemoryModule MemoryModule;
	Trace::FDiagnosticsModule DiagnosticsModule;
};

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

TSharedPtr<Trace::IAnalysisService> FTraceServicesModule::CreateAnalysisService()
{
	checkf(!AnalysisService.IsValid(), TEXT("A AnalysisService already exists."));
	GetModuleService();
	AnalysisService = MakeShared<Trace::FAnalysisService>(*ModuleService.Get());
	return AnalysisService;
}

TSharedPtr<Trace::IModuleService> FTraceServicesModule::CreateModuleService()
{
	checkf(!ModuleService.IsValid(), TEXT("A ModuleService already exists."));
	ModuleService = MakeShared<Trace::FModuleService>();
	return ModuleService;
}

void FTraceServicesModule::StartupModule()
{
	IModularFeatures::Get().RegisterModularFeature(Trace::ModuleFeatureName, &TimingProfilerModule);
	IModularFeatures::Get().RegisterModularFeature(Trace::ModuleFeatureName, &LoadTimeProfilerModule);
#if EXPERIMENTAL_STATSTRACE_ENABLED
	IModularFeatures::Get().RegisterModularFeature(Trace::ModuleFeatureName, &StatsModule);
#endif
	IModularFeatures::Get().RegisterModularFeature(Trace::ModuleFeatureName, &CsvProfilerModule);
	IModularFeatures::Get().RegisterModularFeature(Trace::ModuleFeatureName, &CountersModule);
	IModularFeatures::Get().RegisterModularFeature(Trace::ModuleFeatureName, &NetProfilerModule);
	IModularFeatures::Get().RegisterModularFeature(Trace::ModuleFeatureName, &MemoryModule);
	IModularFeatures::Get().RegisterModularFeature(Trace::ModuleFeatureName, &DiagnosticsModule);
}

void FTraceServicesModule::ShutdownModule()
{
	IModularFeatures::Get().UnregisterModularFeature(Trace::ModuleFeatureName, &DiagnosticsModule);
	IModularFeatures::Get().UnregisterModularFeature(Trace::ModuleFeatureName, &MemoryModule);
	IModularFeatures::Get().UnregisterModularFeature(Trace::ModuleFeatureName, &NetProfilerModule);
	IModularFeatures::Get().UnregisterModularFeature(Trace::ModuleFeatureName, &CountersModule);
	IModularFeatures::Get().UnregisterModularFeature(Trace::ModuleFeatureName, &CsvProfilerModule);
#if EXPERIMENTAL_STATSTRACE_ENABLED
	IModularFeatures::Get().UnregisterModularFeature(Trace::ModuleFeatureName, &StatsModule);
#endif
	IModularFeatures::Get().UnregisterModularFeature(Trace::ModuleFeatureName, &LoadTimeProfilerModule);
	IModularFeatures::Get().UnregisterModularFeature(Trace::ModuleFeatureName, &TimingProfilerModule);

	AnalysisService.Reset();
	ModuleService.Reset();
}

IMPLEMENT_MODULE(FTraceServicesModule, TraceServices)
