// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IStageMonitorSession.h"

#include "StageCriticalEventHandler.h"



/**
 * Implementation of the stage monitor session. Handles new data being received and organizes it for outside access
 */
class FStageMonitorSession : public IStageMonitorSession
{
public:
	FStageMonitorSession(const FString& InSessionName);
	~FStageMonitorSession() = default;
public:

	
	//~Begin IStageMonitorSession interface
	virtual void AddProvider(const FGuid& Identifier, const FStageInstanceDescriptor& Descriptor, const FMessageAddress& Address) override;
	virtual void UpdateProviderDescription(const FGuid& Identifier, const FStageInstanceDescriptor& NewDescriptor, const FMessageAddress& NewAddress) override;
	virtual void AddProviderMessage(UScriptStruct* Type, const FStageProviderMessage* MessageData) override;
	virtual TArray<FMessageAddress> GetProvidersAddress() const override;
	virtual void SetProviderState(const FGuid& Identifier, EStageDataProviderState State) override;
	virtual const TArray<FStageSessionProviderEntry>& GetProviders() const override { return Providers; }
	virtual bool GetProvider(const FGuid& Identifier, FStageSessionProviderEntry& OutProviderEntry) const override;
	virtual void ClearAll() override;
	virtual void GetAllEntries(TArray<TSharedPtr<FStageDataEntry>>& OutEntries) override;
	virtual TSharedPtr<FStageDataEntry> GetLatest(const FGuid& Identifier, UScriptStruct* Type) override;
	virtual EStageDataProviderState GetProviderState(const FGuid& Identifier) override;
	virtual bool IsStageInCriticalState() const override;
	virtual bool IsTimePartOfCriticalState(double TimeInSeconds) const override;
	virtual FName GetCurrentCriticalStateSource() const override;
	virtual TArray<FName> GetCriticalStateHistorySources() const override;
	virtual TArray<FName> GetCriticalStateSources(double TimeInSeconds) const override;
	virtual FString GetSessionName() const override;

	virtual FOnStageSessionNewDataReceived& OnStageSessionNewDataReceived() override { return OnNewDataReceivedDelegate; }
	virtual FOnStageSessionDataCleared& OnStageSessionDataCleared() override { return OnStageSessionDataClearedDelegate; }
	virtual FOnStageDataProviderListChanged& OnStageDataProviderListChanged() override { return OnStageDataProviderListChangedDelegate; }
	//~End IStageMonitorSession interface

private:

	/** Update snapshot for this message type per provider. */
	void UpdateProviderLatestEntry(const FGuid& Identifier, UScriptStruct* Type, TSharedPtr<FStageDataEntry> NewEntry);

	/** Inserts a new message entry in our list, sorted by timecode in seconds */
	void InsertNewEntry(TSharedPtr<FStageDataEntry> NewEntry);

private:
	
	/** List of providers currently monitored */
	TArray<FStageSessionProviderEntry> Providers;

	/** List of all messages received. */
	TArray<TSharedPtr<FStageDataEntry>> Entries;

	/** Latest entry per message type for each provider */
	TMap<FGuid, TArray<TSharedPtr<FStageDataEntry>>> ProviderLatestData;

	/** Manages critical state messages to manage this session's state */
	TUniquePtr<FStageCriticalEventHandler> CriticalEventHandler;

	/** This session name. Used for display */
	FString SessionName;

	/** Delegate triggered when new data was received */
	FOnStageSessionNewDataReceived OnNewDataReceivedDelegate;

	/** Delegate triggered when session data is cleared */
	FOnStageSessionDataCleared OnStageSessionDataClearedDelegate;

	/** Delegate triggered when the list of monitored providers changed */
	FOnStageDataProviderListChanged OnStageDataProviderListChangedDelegate;
};

