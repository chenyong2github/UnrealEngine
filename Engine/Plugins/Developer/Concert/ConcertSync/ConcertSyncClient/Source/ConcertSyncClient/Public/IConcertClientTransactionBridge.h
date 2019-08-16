// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "UObject/StructOnScope.h"
#include "ConcertTransactionEvents.h"
#include "IdentifierTable/ConcertIdentifierTable.h"

struct FConcertSessionVersionInfo;

/**
 * Common data for a transaction.
 */
struct FConcertClientLocalTransactionCommonData
{
	FConcertClientLocalTransactionCommonData(const FText InTransactionTitle, const FGuid& InTransactionId, const FGuid& InOperationId, UObject* InPrimaryObject)
		: TransactionTitle(InTransactionTitle)
		, TransactionId(InTransactionId)
		, OperationId(InOperationId)
		, PrimaryObject(InPrimaryObject)
	{
	}

	FText TransactionTitle;
	FGuid TransactionId;
	FGuid OperationId;
	FWeakObjectPtr PrimaryObject;
	TArray<FName> ModifiedPackages;
	TArray<FConcertObjectId> ExcludedObjectUpdates;
	bool bIsExcluded = false;
};

/**
 * Snapshot data for a transaction.
 */
struct FConcertClientLocalTransactionSnapshotData
{
	TArray<FConcertExportedObject> SnapshotObjectUpdates;
};

/**
 * Finalized data for a transaction.
 */
struct FConcertClientLocalTransactionFinalizedData
{
	FConcertLocalIdentifierTable FinalizedLocalIdentifierTable;
	TArray<FConcertExportedObject> FinalizedObjectUpdates;
	bool bWasCanceled = false;
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnConcertClientLocalTransactionSnapshot, const FConcertClientLocalTransactionCommonData&, const FConcertClientLocalTransactionSnapshotData&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnConcertClientLocalTransactionFinalized, const FConcertClientLocalTransactionCommonData&, const FConcertClientLocalTransactionFinalizedData&);

/**
 * Bridge between the editor transaction system and Concert.
 * Deals with converting local ongoing transactions to Concert transaction data, 
 * and applying remote Concert transaction data onto this local instance.
 */
class IConcertClientTransactionBridge
{
public:
	/** Scoped struct to ignore a local transaction */
	struct FScopedIgnoreLocalTransaction : private TGuardValue<bool>
	{
		FScopedIgnoreLocalTransaction(IConcertClientTransactionBridge& InTransactionBridge)
			: TGuardValue(InTransactionBridge.GetIgnoreLocalTransactionsRef(), true)
		{
		}
	};

	virtual ~IConcertClientTransactionBridge() = default;

	/**
	 * Called when an ongoing transaction is updated via a snapshot.
	 * @note This is called during end-frame processing.
	 */
	virtual FOnConcertClientLocalTransactionSnapshot& OnLocalTransactionSnapshot() = 0;

	/**
	 * Called when an transaction is finalized.
	 * @note This is called during end-frame processing.
	 */
	virtual FOnConcertClientLocalTransactionFinalized& OnLocalTransactionFinalized() = 0;

	/**
	 * Can we currently apply a remote transaction event to this local instance?
	 * @return True if we can apply a remote transaction, false otherwise.
	 */
	virtual bool CanApplyRemoteTransaction() const = 0;

	/**
	 * Apply a remote transaction event to this local instance.
	 * @param InEvent					The event to apply.
	 * @param InVersionInfo				The version information for the serialized data in the event, or null if the event should be serialized using the compiled in version info.
	 * @param InPackagesToProcess		The list of packages to apply changes for, or an empty array to apply all changes.
	 * @param InLocalIdentifierTablePtr The local identifier table for the event data (if any).
	 * @param bIsSnapshot				True if this transaction event was a snapshot rather than a finalized transaction.
	 */
	virtual void ApplyRemoteTransaction(const FConcertTransactionEventBase& InEvent, const FConcertSessionVersionInfo* InVersionInfo, const TArray<FName>& InPackagesToProcess, const FConcertLocalIdentifierTable* InLocalIdentifierTablePtr, const bool bIsSnapshot) = 0;

protected:
	/**
	 * Function to access the internal bool controlling whether local transactions are currently being tracked.
	 * @note Exists to implement FScopedIgnoreLocalTransaction.
	 */
	virtual bool& GetIgnoreLocalTransactionsRef() = 0;
};
