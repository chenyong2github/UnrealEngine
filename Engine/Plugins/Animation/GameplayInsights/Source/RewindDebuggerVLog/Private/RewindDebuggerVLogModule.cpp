// Copyright Epic Games, Inc. All Rights Reserved.

#include "RewindDebuggerVLogModule.h"
#include "Features/IModularFeatures.h"

#define LOCTEXT_NAMESPACE "RewindDebuggerVLogModule"

void FRewindDebuggerVLogModule::StartupModule()
{
	IModularFeatures::Get().RegisterModularFeature(IRewindDebuggerExtension::ModularFeatureName, &RewindDebuggerVLogExtension);
	IModularFeatures::Get().RegisterModularFeature(TraceServices::ModuleFeatureName, &VLogTraceModule);
}

void FRewindDebuggerVLogModule::ShutdownModule()
{
	IModularFeatures::Get().UnregisterModularFeature(IRewindDebuggerExtension::ModularFeatureName, &RewindDebuggerVLogExtension);
	IModularFeatures::Get().UnregisterModularFeature(TraceServices::ModuleFeatureName, &VLogTraceModule);
}

IMPLEMENT_MODULE(FRewindDebuggerVLogModule, RewindDebuggerVLog);

#undef LOCTEXT_NAMESPACE
