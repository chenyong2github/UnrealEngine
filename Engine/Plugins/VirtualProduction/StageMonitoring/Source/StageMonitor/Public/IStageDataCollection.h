// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Delegates/DelegateCombinations.h"
#include "IMessageContext.h"
#include "StageMessages.h"
#include "UObject/StructOnScope.h"


/**
 * Entry corresponding to a provider we are monitoring
 * Contains information related to the provider so we can communicate with it
 * and more dynamic information like last communication received
 */
struct STAGEMONITOR_API FCollectionProviderEntry
{
	FCollectionProviderEntry() = default;

	bool operator==(const FCollectionProviderEntry& Other) const { return Identifier == Other.Identifier; }
	bool operator!=(const FCollectionProviderEntry& Other) const { return !(*this == Other); }

	/** Identifier of this provider */
	FGuid Identifier;

	/** Detailed descriptor */
	FStageInstanceDescriptor Descriptor;

	/** Address of this provider */
	FMessageAddress Address;

	/** State of this provider based on message reception */
	EStageDataProviderState State = EStageDataProviderState::Closed;

	/** Timestamp when last message was received based on FApp::GetCurrentTime */
	double LastReceivedMessageTime = 0.0;
};

/**
 * Data entry containing data received from a provider
 */
struct STAGEMONITOR_API FStageDataEntry
{
	TSharedPtr<FStructOnScope> Data;
	double MessageTime = 0.0;
};

/**
 * Interface describing a collection of data received by the monitor
 */
class STAGEMONITOR_API IStageDataCollection
{
public:

	virtual ~IStageDataCollection() {}

	/**
	 * Adds a message to the collection
	 */
	virtual void AddProviderMessage(UScriptStruct* Type, const FStageProviderMessage* MessageData) = 0;


	/**
	 * Get all providers that have been connected to the monitor
	 */
	virtual const TArray<FCollectionProviderEntry>& GetProviders() const = 0;

	/**
	 * Get ProviderEntry associated to an identifier
	 */
	virtual bool GetProvider(const FGuid& Identifier, FCollectionProviderEntry& OutProviderEntry) const = 0;

	/**
	 * Clear every activities of the collection
	 */
	virtual void ClearAll() = 0;

	/**
	 * Returns all entries that have been received
	 */
	virtual void GetAllEntries(TArray<TSharedPtr<FStageDataEntry>>& OutEntries) = 0;

	/**
	 * Returns the state of the desired provider
	 */
	virtual TSharedPtr<FStageDataEntry> GetLatest(const FGuid& Identifier, UScriptStruct* Type) = 0;

	/**
	 * Returns the state of the desired provider
	 */
	virtual EStageDataProviderState GetProviderState(const FGuid& Identifier) = 0;


	/**
	 * Callback triggered when new data has been added to the collection
	 */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnStageDataCollectionNewDataReceived, TSharedPtr<FStageDataEntry> /*Data*/);
	virtual FOnStageDataCollectionNewDataReceived& OnStageDataCollectionNewDataReceived() = 0;

	/**
	 * Callback triggered when data from the collection has been cleared
	 */
	DECLARE_MULTICAST_DELEGATE(FOnStageDataCollectionCleared);
	virtual FOnStageDataCollectionCleared& OnStageDataCollectionCleared() = 0;

	/**
     * Callback triggered when provider list changed
	 */
	DECLARE_MULTICAST_DELEGATE(FOnStageDataProviderListChanged);
	virtual FOnStageDataProviderListChanged& OnStageDataProviderListChanged() = 0;
};
