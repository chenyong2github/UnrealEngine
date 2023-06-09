// Copyright Epic Games, Inc. All Rights Reserved.

#include "10XSourceCodeAccessModule.h"
#include "Features/IModularFeatures.h"
#include "Modules/ModuleManager.h"
#include "HAL/LowLevelMemTracker.h"
#include "10XSourceCodeAccessor.h"

LLM_DEFINE_TAG(N10XSourceCodeAccess);

IMPLEMENT_MODULE( F10XSourceCodeAccessModule, 10XSourceCodeAccess );

F10XSourceCodeAccessModule::F10XSourceCodeAccessModule()
	: SourceCodeAccessor(MakeShareable(new F10XSourceCodeAccessor()))
{
}

void F10XSourceCodeAccessModule::StartupModule()
{
	LLM_SCOPE_BYTAG(N10XSourceCodeAccess);

	SourceCodeAccessor->Startup();

	IModularFeatures::Get().RegisterModularFeature(TEXT("SourceCodeAccessor"), &SourceCodeAccessor.Get() );
}

void F10XSourceCodeAccessModule::ShutdownModule()
{
	IModularFeatures::Get().UnregisterModularFeature(TEXT("SourceCodeAccessor"), &SourceCodeAccessor.Get());

	SourceCodeAccessor->Shutdown();
}

F10XSourceCodeAccessor& F10XSourceCodeAccessModule::GetAccessor()
{
	return SourceCodeAccessor.Get();
}
