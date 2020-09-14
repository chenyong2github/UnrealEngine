// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Delegates/DelegateCombinations.h"
#include "IMessageContext.h"
#include "StageMessages.h"
#include "UObject/StructOnScope.h"

#include "IStageMonitorSession.generated.h"

/**
 * Entry corresponding to a provider we are monitoring
 * Contains information related to the provider so we can communicate with it
 * and more dynamic information like last communication received
 */
 USTRUCT()
struct STAGEMONITOR_API FStageSessionProviderEntry
{
	GENERATED_BODY()

public:
	FStageSessionProviderEntry() = default;

	bool operator==(const FStageSessionProviderEntry& Other) const { return Identifier == Other.Identifier; }
	bool operator!=(const FStageSessionProviderEntry& Other) const { return !(*this == Other); }

	/** Identifier of this provider */
	UPROPERTY()
	FGuid Identifier;

	/** Detailed descriptor */
	UPROPERTY()
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
 * Interface describing a session of data received by the monitor
 * Can be exported and reimported for future analysis
 */
class STAGEMONITOR_API IStageMonitorSession
{
public:

	virtual ~IStageMonitorSession() {}

	/** 
	 * Adds a new provider to the ones we're handling data for 
	 */
	virtual void AddProvider(const FGuid& Identifier, const FStageInstanceDescriptor& Descriptor, const FMessageAddress& Address) = 0;

	/** 
	 * Updates a provider's description. Can happen if was closed and discovered again. 
	 */
	virtual void UpdateProviderDescription(const FGuid& Identifier, const FStageInstanceDescriptor& NewDescriptor, const FMessageAddress& NewAddress) = 0;

	/** Returns the addresses of the providers currently monitored */
	virtual TArray<FMessageAddress> GetProvidersAddress() const = 0;

	/** 
	 * Changes a provider's monitoring state 
	 */
	virtual void SetProviderState(const FGuid& Identifier, EStageDataProviderState State) = 0;

	/**
	 * Adds a message to the session
	 */
	virtual void AddProviderMessage(UScriptStruct* Type, const FStageProviderMessage* MessageData) = 0;

	/**
	 * Get all providers that have been connected to the monitor
	 */
	virtual const TArray<FStageSessionProviderEntry>& GetProviders() const = 0;

	/**
	 * Get ProviderEntry associated to an identifier
	 */
	virtual bool GetProvider(const FGuid& Identifier, FStageSessionProviderEntry& OutProviderEntry) const = 0;

	/**
	 * Clear every activities of the session
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
	 * Returns true if stage monitor currently in critical state (i.e. recording)
	 */
	virtual bool IsStageInCriticalState() const = 0;

	/**
	 * Returns true if the given time is part of a critical state time range.
	 */
	virtual bool IsTimePartOfCriticalState(double TimeInSeconds) const = 0;

	/**
	 * Returns Source name of the current critical state. Returns None if not active
	 */
	virtual FName GetCurrentCriticalStateSource() const = 0;

	/**
	 * Returns a list of all sources that triggered a critical state
	 */
	virtual TArray<FName> GetCriticalStateHistorySources() const = 0;

	/**
	 * Returns a list of all sources that triggered a critical state during TimeInSeconds.
	 * If provided time is not part of a critical state, returned array will be empty
	 */
	virtual TArray<FName> GetCriticalStateSources(double TimeInSeconds) const = 0;

	/**
	 * Returns the name of that session.
	 */
	virtual FString GetSessionName() const = 0;


	/**
	 * Callback triggered when new data has been added to the session
	 */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnStageSessionNewDataReceived, TSharedPtr<FStageDataEntry> /*Data*/);
	virtual FOnStageSessionNewDataReceived& OnStageSessionNewDataReceived() = 0;

	/**
	 * Callback triggered when data from the session has been cleared
	 */
	DECLARE_MULTICAST_DELEGATE(FOnStageSessionDataCleared);
	virtual FOnStageSessionDataCleared& OnStageSessionDataCleared() = 0;

	/**
     * Callback triggered when provider list changed
	 */
	DECLARE_MULTICAST_DELEGATE(FOnStageDataProviderListChanged);
	virtual FOnStageDataProviderListChanged& OnStageDataProviderListChanged() = 0;
};
