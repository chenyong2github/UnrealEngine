// Copyright Epic Games, Inc. All Rights Reserved.

#include "PerforceSourceControlModule.h"
#include "Misc/App.h"
#include "PerforceSourceControlOperations.h"
#include "Features/IModularFeatures.h"

#define LOCTEXT_NAMESPACE "PerforceSourceControl"

template<typename Type>
static TSharedRef<IPerforceSourceControlWorker, ESPMode::ThreadSafe> CreateWorker()
{
	return MakeShareable( new Type() );
}

void FPerforceSourceControlModule::StartupModule()
{
	// Register our operations
	PerforceSourceControlProvider.RegisterWorker( "Connect", FGetPerforceSourceControlWorker::CreateStatic( &CreateWorker<FPerforceConnectWorker> ) );
	PerforceSourceControlProvider.RegisterWorker( "CheckOut", FGetPerforceSourceControlWorker::CreateStatic( &CreateWorker<FPerforceCheckOutWorker> ) );
	PerforceSourceControlProvider.RegisterWorker( "UpdateStatus", FGetPerforceSourceControlWorker::CreateStatic( &CreateWorker<FPerforceUpdateStatusWorker> ) );
	PerforceSourceControlProvider.RegisterWorker( "MarkForAdd", FGetPerforceSourceControlWorker::CreateStatic( &CreateWorker<FPerforceMarkForAddWorker> ) );
	PerforceSourceControlProvider.RegisterWorker( "Delete", FGetPerforceSourceControlWorker::CreateStatic( &CreateWorker<FPerforceDeleteWorker> ) );
	PerforceSourceControlProvider.RegisterWorker( "Revert", FGetPerforceSourceControlWorker::CreateStatic( &CreateWorker<FPerforceRevertWorker> ) );
	PerforceSourceControlProvider.RegisterWorker( "Sync", FGetPerforceSourceControlWorker::CreateStatic( &CreateWorker<FPerforceSyncWorker> ) );
	PerforceSourceControlProvider.RegisterWorker( "CheckIn", FGetPerforceSourceControlWorker::CreateStatic( &CreateWorker<FPerforceCheckInWorker> ) );
	PerforceSourceControlProvider.RegisterWorker( "GetWorkspaces", FGetPerforceSourceControlWorker::CreateStatic( &CreateWorker<FPerforceGetWorkspacesWorker> ) );
	PerforceSourceControlProvider.RegisterWorker( "Copy", FGetPerforceSourceControlWorker::CreateStatic( &CreateWorker<FPerforceCopyWorker> ) );
	PerforceSourceControlProvider.RegisterWorker( "Resolve", FGetPerforceSourceControlWorker::CreateStatic( &CreateWorker<FPerforceResolveWorker> ) );
	PerforceSourceControlProvider.RegisterWorker( "ChangeStatus", FGetPerforceSourceControlWorker::CreateStatic(&CreateWorker<FPerforceChangeStatusWorker>));
	PerforceSourceControlProvider.RegisterWorker( "UpdateChangelistsStatus", FGetPerforceSourceControlWorker::CreateStatic( &CreateWorker<FPerforceGetPendingChangelistsWorker> ) );
	PerforceSourceControlProvider.RegisterWorker( "NewChangelist", FGetPerforceSourceControlWorker::CreateStatic( &CreateWorker<FPerforceNewChangelistWorker> ) );
	PerforceSourceControlProvider.RegisterWorker( "DeleteChangelist", FGetPerforceSourceControlWorker::CreateStatic( &CreateWorker<FPerforceDeleteChangelistWorker> ) );
	PerforceSourceControlProvider.RegisterWorker( "EditChangelist", FGetPerforceSourceControlWorker::CreateStatic( &CreateWorker<FPerforceEditChangelistWorker> ) );
	PerforceSourceControlProvider.RegisterWorker( "RevertUnchanged", FGetPerforceSourceControlWorker::CreateStatic( &CreateWorker<FPerforceRevertUnchangedWorker> ) );
	PerforceSourceControlProvider.RegisterWorker( "MoveToChangelist", FGetPerforceSourceControlWorker::CreateStatic( &CreateWorker<FPerforceReopenWorker>) );
	PerforceSourceControlProvider.RegisterWorker( "Shelve", FGetPerforceSourceControlWorker::CreateStatic( &CreateWorker<FPerforceShelveWorker>) );
	PerforceSourceControlProvider.RegisterWorker( "Unshelve", FGetPerforceSourceControlWorker::CreateStatic( &CreateWorker<FPerforceUnshelveWorker>) );
	PerforceSourceControlProvider.RegisterWorker( "DeleteShelved", FGetPerforceSourceControlWorker::CreateStatic( &CreateWorker<FPerforceDeleteShelveWorker>) );
	PerforceSourceControlProvider.RegisterWorker( "DownloadFile", FGetPerforceSourceControlWorker::CreateStatic( &CreateWorker<FPerforceDownloadFileWorker>) );
	PerforceSourceControlProvider.RegisterWorker( "CreateWorkspace", FGetPerforceSourceControlWorker::CreateStatic( &CreateWorker<FPerforceCreateWorkspaceWorker>) );
	PerforceSourceControlProvider.RegisterWorker( "DeleteWorkspace", FGetPerforceSourceControlWorker::CreateStatic( &CreateWorker<FPerforceDeleteWorkspaceWorker>) );

	// load our settings
	PerforceSourceControlSettings.LoadSettings();

	// Bind our source control provider to the editor
	IModularFeatures::Get().RegisterModularFeature( "SourceControl", &PerforceSourceControlProvider );
}

void FPerforceSourceControlModule::ShutdownModule()
{
	// shut down the provider, as this module is going away
	PerforceSourceControlProvider.Close();

	// unbind provider from editor
	IModularFeatures::Get().UnregisterModularFeature( "SourceControl", &PerforceSourceControlProvider );
}

FPerforceSourceControlSettings& FPerforceSourceControlModule::AccessSettings()
{
	return PerforceSourceControlSettings;
}

void FPerforceSourceControlModule::SaveSettings()
{
	if (FApp::IsUnattended() || IsRunningCommandlet())
	{
		return;
	}

	PerforceSourceControlSettings.SaveSettings();
}

void FPerforceSourceControlModule::SetLastErrors(const TArray<FText>& InErrors)
{
	FPerforceSourceControlModule* Module = FModuleManager::GetModulePtr<FPerforceSourceControlModule>("PerforceSourceControl");
	if (Module)
	{
		Module->GetProvider().SetLastErrors(InErrors);
	}
}

IMPLEMENT_MODULE(FPerforceSourceControlModule, PerforceSourceControl);

#undef LOCTEXT_NAMESPACE
