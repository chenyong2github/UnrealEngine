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
#include "Modules/PlatformEventsModule.h"
#include "Modules/TasksModule.h"
#include "Stats/StatsTrace.h"

class FTraceServicesModule
	: public ITraceServicesModule
{
public:
	virtual TSharedPtr<TraceServices::IAnalysisService> GetAnalysisService() override;
	virtual TSharedPtr<TraceServices::IModuleService> GetModuleService() override;
	virtual TSharedPtr<TraceServices::IAnalysisService> CreateAnalysisService() override;
	virtual TSharedPtr<TraceServices::IModuleService> CreateModuleService() override;

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TSharedPtr<TraceServices::FAnalysisService> AnalysisService;
	TSharedPtr<TraceServices::FModuleService> ModuleService;

	TraceServices::FTimingProfilerModule TimingProfilerModule;
	TraceServices::FLoadTimeProfilerModule LoadTimeProfilerModule;
	TraceServices::FStatsModule StatsModule;
	TraceServices::FCsvProfilerModule CsvProfilerModule;
	TraceServices::FCountersModule CountersModule;
	TraceServices::FNetProfilerModule NetProfilerModule;
	TraceServices::FMemoryModule MemoryModule;
	TraceServices::FDiagnosticsModule DiagnosticsModule;
	TraceServices::FPlatformEventsModule PlatformEventsModule;
	TraceServices::FTasksModule TasksModule;
};

TSharedPtr<TraceServices::IAnalysisService> FTraceServicesModule::GetAnalysisService()
{
	if (!AnalysisService.IsValid())
	{
		GetModuleService();
		AnalysisService = MakeShared<TraceServices::FAnalysisService>(*ModuleService.Get());
	}
	return AnalysisService;
}

TSharedPtr<TraceServices::IModuleService> FTraceServicesModule::GetModuleService()
{
	if (!ModuleService.IsValid())
	{
		ModuleService = MakeShared<TraceServices::FModuleService>();
	}
	return ModuleService;
}

TSharedPtr<TraceServices::IAnalysisService> FTraceServicesModule::CreateAnalysisService()
{
	checkf(!AnalysisService.IsValid(), TEXT("A AnalysisService already exists."));
	GetModuleService();
	AnalysisService = MakeShared<TraceServices::FAnalysisService>(*ModuleService.Get());
	return AnalysisService;
}

TSharedPtr<TraceServices::IModuleService> FTraceServicesModule::CreateModuleService()
{
	checkf(!ModuleService.IsValid(), TEXT("A ModuleService already exists."));
	ModuleService = MakeShared<TraceServices::FModuleService>();
	return ModuleService;
}

void FTraceServicesModule::StartupModule()
{
	IModularFeatures::Get().RegisterModularFeature(TraceServices::ModuleFeatureName, &TimingProfilerModule);
	IModularFeatures::Get().RegisterModularFeature(TraceServices::ModuleFeatureName, &CsvProfilerModule);
	IModularFeatures::Get().RegisterModularFeature(TraceServices::ModuleFeatureName, &CountersModule);
	IModularFeatures::Get().RegisterModularFeature(TraceServices::ModuleFeatureName, &NetProfilerModule);
	IModularFeatures::Get().RegisterModularFeature(TraceServices::ModuleFeatureName, &DiagnosticsModule);
	IModularFeatures::Get().RegisterModularFeature(TraceServices::ModuleFeatureName, &PlatformEventsModule);
	IModularFeatures::Get().RegisterModularFeature(TraceServices::ModuleFeatureName, &StatsModule);
	IModularFeatures::Get().RegisterModularFeature(TraceServices::ModuleFeatureName, &LoadTimeProfilerModule);
	IModularFeatures::Get().RegisterModularFeature(TraceServices::ModuleFeatureName, &MemoryModule);
	IModularFeatures::Get().RegisterModularFeature(TraceServices::ModuleFeatureName, &TasksModule);
}

void FTraceServicesModule::ShutdownModule()
{
	IModularFeatures::Get().UnregisterModularFeature(TraceServices::ModuleFeatureName, &TasksModule);
	IModularFeatures::Get().UnregisterModularFeature(TraceServices::ModuleFeatureName, &MemoryModule);
	IModularFeatures::Get().UnregisterModularFeature(TraceServices::ModuleFeatureName, &LoadTimeProfilerModule);
	IModularFeatures::Get().UnregisterModularFeature(TraceServices::ModuleFeatureName, &StatsModule);
	IModularFeatures::Get().UnregisterModularFeature(TraceServices::ModuleFeatureName, &PlatformEventsModule);
	IModularFeatures::Get().UnregisterModularFeature(TraceServices::ModuleFeatureName, &DiagnosticsModule);
	IModularFeatures::Get().UnregisterModularFeature(TraceServices::ModuleFeatureName, &NetProfilerModule);
	IModularFeatures::Get().UnregisterModularFeature(TraceServices::ModuleFeatureName, &CountersModule);
	IModularFeatures::Get().UnregisterModularFeature(TraceServices::ModuleFeatureName, &CsvProfilerModule);
	IModularFeatures::Get().UnregisterModularFeature(TraceServices::ModuleFeatureName, &TimingProfilerModule);

	AnalysisService.Reset();
	ModuleService.Reset();
}

IMPLEMENT_MODULE(FTraceServicesModule, TraceServices)
