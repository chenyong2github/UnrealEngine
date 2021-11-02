// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeinSourceControlModule.h"
#include "Modules/ModuleManager.h"
#include "SkeinSourceControlOperations.h"
#include "Features/IModularFeatures.h"

#define LOCTEXT_NAMESPACE "SkeinSourceControl"

template<typename Type>
static TSharedRef<ISkeinSourceControlWorker, ESPMode::ThreadSafe> CreateWorker()
{
	return MakeShareable(new Type());
}

void FSkeinSourceControlModule::StartupModule()
{
	// Register our operations
	SkeinSourceControlProvider.RegisterWorker("Connect", FGetSkeinSourceControlWorker::CreateStatic(&CreateWorker<FSkeinConnectWorker>));
	SkeinSourceControlProvider.RegisterWorker("CheckIn", FGetSkeinSourceControlWorker::CreateStatic(&CreateWorker<FSkeinCheckInWorker>));
	SkeinSourceControlProvider.RegisterWorker("MarkForAdd", FGetSkeinSourceControlWorker::CreateStatic(&CreateWorker<FSkeinMarkForAddWorker>));
	SkeinSourceControlProvider.RegisterWorker("Delete", FGetSkeinSourceControlWorker::CreateStatic(&CreateWorker<FSkeinDeleteWorker>));
	SkeinSourceControlProvider.RegisterWorker("Revert", FGetSkeinSourceControlWorker::CreateStatic(&CreateWorker<FSkeinRevertWorker>));
	SkeinSourceControlProvider.RegisterWorker("Sync", FGetSkeinSourceControlWorker::CreateStatic(&CreateWorker<FSkeinSyncWorker>));
	SkeinSourceControlProvider.RegisterWorker("UpdateStatus", FGetSkeinSourceControlWorker::CreateStatic(&CreateWorker<FSkeinUpdateStatusWorker>));

//	SkeinSourceControlProvider.RegisterWorker("Copy", FGetSkeinSourceControlWorker::CreateStatic(&CreateWorker<FSkeinCopyWorker>));
//	SkeinSourceControlProvider.RegisterWorker("Resolve", FGetSkeinSourceControlWorker::CreateStatic(&CreateWorker<FSkeinResolveWorker>));

	// Bind our source control provider to the editor
	IModularFeatures::Get().RegisterModularFeature("SourceControl", &SkeinSourceControlProvider);
}

void FSkeinSourceControlModule::ShutdownModule()
{
	// Shut down the provider, as this module is going away
	SkeinSourceControlProvider.Close();

	// Unbind provider from editor
	IModularFeatures::Get().UnregisterModularFeature("SourceControl", &SkeinSourceControlProvider);
}

IMPLEMENT_MODULE(FSkeinSourceControlModule, SkeinSourceControl);

#undef LOCTEXT_NAMESPACE
