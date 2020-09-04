// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISourceControlProvider.h" // for ECommandResult

DECLARE_DELEGATE_TwoParams(FSourceControlConnectComplete, bool, FString);
DECLARE_DELEGATE_ThreeParams(FSourceControlReportRevisionComplete, bool, FString, FString);
DECLARE_DELEGATE_ThreeParams(FSourceControlSyncComplete, bool, FString, FString);

class FSwitchboardSourceControl
{
public:
	bool Connect(const FString& InSCCProviderName, const TMap<FString, FString>& InSCCSettings);
	bool ReportRevision(const FString& InPath);
	bool Sync(const FString& InPath, const FString& InRevision);

	const FString& GetLastError() const { return LastError; }

	FSourceControlConnectComplete ConnectCompleteDelegate;
	FSourceControlReportRevisionComplete ReportRevisionCompleteDelegate;
	FSourceControlSyncComplete SyncCompleteDelegate;

private:
	void OnConnectFinished(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult);
	void OnChangelistStatusFinished(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult);
	void OnSyncFinished(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult);

	bool IsCommandInProgress();
	ISourceControlProvider* GetProvider();

private:
	FString SCCProviderName = TEXT("Perforce");
	FName CommandInProgress;
	FString LastError;
};
