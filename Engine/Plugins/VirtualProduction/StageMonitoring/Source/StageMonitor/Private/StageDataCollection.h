// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IStageDataCollection.h"


/**
 * Implementation of the stage data collection. Handles new data being received and organizes it for outside access
 */
class FStageDataCollection : public IStageDataCollection
{
public:

	/** Adds a new provider to the ones we're handling data for */
	void AddProvider(const FGuid& Identifier, const FStageInstanceDescriptor& Descriptor, const FMessageAddress& Address);

	/** Updates a provider's description. Can happen if was closed and discovered again. */
	void UpdateProviderDescription(const FGuid& Identifier, const FStageInstanceDescriptor& NewDescriptor, const FMessageAddress& NewAddress);
	
	/** Adds a new message coming from a provider. */
	void AddProviderMessage(UScriptStruct* Type, const FStageProviderMessage* MessageData);	

	/** Returns the addresses of the providers currently monitored */
	TArray<FMessageAddress> GetProvidersAddress() const;

	/** Changes a provider's monitoring state */
	void SetProviderState(const FGuid& Identifier, EStageDataProviderState State);
	
	//~Begin IStageDataCollection interface
	virtual const TArray<FCollectionProviderEntry>& GetProviders() const override { return Providers; }
	virtual bool GetProvider(const FGuid& Identifier, FCollectionProviderEntry& OutProviderEntry) const override;
	virtual void ClearAll() override;
	virtual void GetAllEntries(TArray<TSharedPtr<FStageDataEntry>>& OutEntries) override;
	virtual TSharedPtr<FStageDataEntry> GetLatest(const FGuid& Identifier, UScriptStruct* Type) override;
	virtual EStageDataProviderState GetProviderState(const FGuid& Identifier) override;

	virtual FOnStageDataCollectionNewDataReceived& OnStageDataCollectionNewDataReceived() override { return OnNewDataReceivedDelegate; }
	virtual FOnStageDataCollectionCleared& OnStageDataCollectionCleared() override { return OnStageDataClearedDelegate; }
	virtual FOnStageDataProviderListChanged& OnStageDataProviderListChanged() override { return OnStageDataProviderListChangedDelegate; }
	//~End IStageDataCollection interface

private:

	/** Update snapshot for this message type per provider. */
	void UpdateProviderLatestEntry(const FGuid& Identifier, UScriptStruct* Type, TSharedPtr<FStageDataEntry> NewEntry);

	/** Inserts a new message entry in our list, sorted by timecode in seconds */
	void InsertNewEntry(TSharedPtr<FStageDataEntry> NewEntry);

private:
	
	/** List of providers currently monitored */
	TArray<FCollectionProviderEntry> Providers;

	/** List of all messages received. */
	TArray<TSharedPtr<FStageDataEntry>> Entries;

	/** Latest entry per message type for each provider */
	TMap<FGuid, TArray<TSharedPtr<FStageDataEntry>>> ProviderLatestData;

	/** Delegate triggered when new data was received */
	FOnStageDataCollectionNewDataReceived OnNewDataReceivedDelegate;

	/** Delegate triggered when collection is cleared */
	FOnStageDataCollectionCleared OnStageDataClearedDelegate;

	/** Delegate triggered when the list of monitored providers changed */
	FOnStageDataProviderListChanged OnStageDataProviderListChangedDelegate;
};

