// Copyright Epic Games, Inc. All Rights Reserved.

#include "SwitchboardSourceControl.h"

#include "SwitchboardListenerApp.h"

#include "ISourceControlModule.h"
#include "PerforceSourceControlChangeStatusOperation.h"
#include "SourceControlHelpers.h"
#include "SourceControlOperations.h"

namespace
{
	FString PluginNameFromSCCName(const FName SCCName)
	{
		return SCCName.ToString() + TEXT("SourceControl");
	}
}

bool FSwitchboardSourceControl::Connect(const FString& InSCCProviderName, const TMap<FString, FString>& InSCCSettings)
{
	if (IsCommandInProgress())
	{
		return false;
	}

	{ // unload current plugin, this is done even if we later load the same plugin again in order to update the plugin's settings
		const FString CurrentPluginName = PluginNameFromSCCName(*SCCProviderName);
		FModuleManager::Get().UnloadModule(*CurrentPluginName);
	}

	SCCProviderName = InSCCProviderName;
	const FString PluginName = PluginNameFromSCCName(*SCCProviderName);

	// write all settings that were passed in with the command into the global config.
	// this relies on the correctness of the setting names which are private to the respective scc module, so we can't check.
	const FString SettingsEntry = FString::Printf(TEXT("%s.%sSettings"), *PluginName, *PluginName);
	const FString& IniFile = SourceControlHelpers::GetSettingsIni();
	for (const auto& Setting: InSCCSettings)
	{
		GConfig->SetString(*SettingsEntry, *Setting.Key, *Setting.Value, IniFile);
	}

	// now load the plugin, which will use the settings we just stored in GConfig
	EModuleLoadResult LoadResult;
	FModuleManager::Get().LoadModuleWithFailureReason(*PluginName, LoadResult);
	if (LoadResult != EModuleLoadResult::Success)
	{
		const FString ModuleError = FString::Printf(TEXT("Could not load version control plugin %s!"), *PluginName);
		UE_LOG(LogSwitchboard, Error, TEXT("%s"), *ModuleError);
		LastError = ModuleError;
		return false;
	}

	ISourceControlModule& SourceControlModule = ISourceControlModule::Get();
	ISourceControlModule::Get().SetProvider(*SCCProviderName);

	ISourceControlProvider& SourceControlProvider = SourceControlModule.GetProvider();
	if (!SourceControlProvider.IsEnabled())
	{
		const FString ProviderError = FString::Printf(TEXT("Could not find source control provider %s!"), *SCCProviderName);
		UE_LOG(LogSwitchboard, Error, TEXT("%s"), *ProviderError);
		LastError = ProviderError;
		return false;
	}

	CommandInProgress = TEXT("Connect");
	const ECommandResult::Type CommandQueued = SourceControlProvider.Login(FString(), EConcurrency::Asynchronous,
		FSourceControlOperationComplete::CreateRaw(this, &FSwitchboardSourceControl::OnConnectFinished));

	if (CommandQueued != ECommandResult::Succeeded)
	{
		CommandInProgress = FName();
		return false;
	}

	return true;
}

bool FSwitchboardSourceControl::ReportRevision(const FString& InPath)
{
	if (IsCommandInProgress())
	{
		return false;
	}

	ISourceControlProvider* Provider = GetProvider();
	if (!Provider)
	{
		return false;
	}

	TSharedRef<FPerforceSourceControlChangeStatusOperation, ESPMode::ThreadSafe> StatusOperation = ISourceControlOperation::Create<FPerforceSourceControlChangeStatusOperation>();
	CommandInProgress = StatusOperation->GetName();

	const ECommandResult::Type CommandQueued = Provider->Execute(StatusOperation, InPath, EConcurrency::Asynchronous,
		FSourceControlOperationComplete::CreateRaw(this, &FSwitchboardSourceControl::OnChangelistStatusFinished));

	if (CommandQueued != ECommandResult::Succeeded)
	{
		CommandInProgress = FName();
		return false;
	}

	return true;
}

bool FSwitchboardSourceControl::Sync(const FString& InPath, const FString& InRevision)
{
	if (IsCommandInProgress())
	{
		return false;
	}

	ISourceControlProvider* Provider = GetProvider();
	if (!Provider)
	{
		return false;
	}

	TSharedRef<FSync, ESPMode::ThreadSafe> SyncOperation = ISourceControlOperation::Create<FSync>();
	SyncOperation->SetRevision(InRevision);

	CommandInProgress = SyncOperation->GetName();
	const ECommandResult::Type CommandQueued = Provider->Execute(SyncOperation, InPath, EConcurrency::Asynchronous,
		FSourceControlOperationComplete::CreateRaw(this, &FSwitchboardSourceControl::OnSyncFinished));

	if (CommandQueued != ECommandResult::Succeeded)
	{
		CommandInProgress = FName();
		return false;
	}

	UE_LOG(LogSwitchboard, Display, TEXT("Started %s sync"), *SCCProviderName);
	return true;
}

bool FSwitchboardSourceControl::IsCommandInProgress()
{
	if (CommandInProgress.IsNone())
	{
		return false;
	}

	const FString InProgressError = FString::Printf(TEXT("%s %s operation is still in progress!"), *SCCProviderName, *CommandInProgress.ToString());
	UE_LOG(LogSwitchboard, Error, TEXT("%s"), *InProgressError);
	LastError = InProgressError;
	return true;
}

ISourceControlProvider* FSwitchboardSourceControl::GetProvider()
{
	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	if (!SourceControlProvider.IsEnabled() || !SourceControlProvider.IsAvailable())
	{
		const FString Error = FString::Printf(TEXT("%s is not connected!"), *SCCProviderName);
		UE_LOG(LogSwitchboard, Error, TEXT("%s"), *Error);
		LastError = Error;
		return nullptr;
	}
	return &SourceControlProvider;
}

void FSwitchboardSourceControl::OnConnectFinished(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult)
{
	const FName OpName = InOperation->GetName();
	check(CommandInProgress == OpName);
	CommandInProgress = FName();

	const bool bSuccess = (InResult == ECommandResult::Succeeded);
	FString ErrorMessage;
	if (bSuccess)
	{
		UE_LOG(LogSwitchboard, Display, TEXT("%s %s operation completed successfully"), *SCCProviderName, *OpName.ToString());
	}
	else
	{
		const FSourceControlResultInfo& Result = InOperation->GetResultInfo();
		ErrorMessage = FString::Printf(TEXT("%s %s operation did not succeed!\n"), *SCCProviderName, *OpName.ToString());
		UE_LOG(LogSwitchboard, Error, TEXT("%s"), *ErrorMessage);

		for (const FText& Error : Result.ErrorMessages)
		{
			ErrorMessage += Error.ToString();
		}
		ErrorMessage.TrimEndInline();
		LastError = ErrorMessage;
	}
	
	ConnectCompleteDelegate.ExecuteIfBound(bSuccess, ErrorMessage);
}

void FSwitchboardSourceControl::OnChangelistStatusFinished(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult)
{
	const FName OpName = InOperation->GetName();
	check(CommandInProgress == OpName);
	CommandInProgress = FName();

	const bool bSuccess = (InResult == ECommandResult::Succeeded);
	FString ErrorMessage;
	FString LatestChangelist = TEXT("0");

	if (bSuccess)
	{
		TSharedRef<FPerforceSourceControlChangeStatusOperation, ESPMode::ThreadSafe> StatusOperation = StaticCastSharedRef<FPerforceSourceControlChangeStatusOperation>(InOperation);

		// find latest changelist entry that we "have"
		for (int32 i=StatusOperation->OutResults.Num()-1; i>=0; --i)
		{
			FChangelistStatusEntry Entry = StatusOperation->OutResults[i];
			if (Entry.Status == EChangelistStatus::Have)
			{
				LatestChangelist = Entry.ChangelistNumber;
				break;
			}

		}
		UE_LOG(LogSwitchboard, Display, TEXT("%s %s operation completed successfully"), *SCCProviderName, *OpName.ToString());
	}
	else
	{
		const FSourceControlResultInfo& Result = InOperation->GetResultInfo();
		ErrorMessage = FString::Printf(TEXT("%s %s operation did not succeed!\n"), *SCCProviderName, *OpName.ToString());
		UE_LOG(LogSwitchboard, Error, TEXT("%s"), *ErrorMessage);

		for (const FText& Error : Result.ErrorMessages)
		{
			ErrorMessage += Error.ToString();
		}
		ErrorMessage.TrimEndInline();
	}

	ReportRevisionCompleteDelegate.ExecuteIfBound(bSuccess, LatestChangelist, ErrorMessage);
}

void FSwitchboardSourceControl::OnSyncFinished(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult)
{
	const FName OpName = InOperation->GetName();
	check(CommandInProgress == OpName);
	CommandInProgress = FName();

	const bool bSuccess = (InResult == ECommandResult::Succeeded);
	FString ErrorMessage;
	FString SyncedChange = TEXT("0");

	if (bSuccess)
	{
		TSharedRef<FSync, ESPMode::ThreadSafe> SyncOperation = StaticCastSharedRef<FSync>(InOperation);
		SyncedChange = SyncOperation->GetRevision();

		UE_LOG(LogSwitchboard, Display, TEXT("%s %s operation completed successfully"), *SCCProviderName, *OpName.ToString());
	}
	else
	{
		const FSourceControlResultInfo& Result = InOperation->GetResultInfo();
		ErrorMessage = FString::Printf(TEXT("%s %s operation did not succeed!\n"), *SCCProviderName, *OpName.ToString());
		UE_LOG(LogSwitchboard, Error, TEXT("%s"), *ErrorMessage);

		for (const FText& Error : Result.ErrorMessages)
		{
			ErrorMessage += Error.ToString();
		}
		ErrorMessage.TrimEndInline();
	}

	SyncCompleteDelegate.ExecuteIfBound(bSuccess, SyncedChange, ErrorMessage);
}
