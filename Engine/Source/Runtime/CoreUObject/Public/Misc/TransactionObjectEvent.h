// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Misc/Guid.h"

/** Delta-change information for an object that was transacted */
struct FTransactionObjectDeltaChange
{
	FTransactionObjectDeltaChange()
		: bHasNameChange(false)
		, bHasOuterChange(false)
		, bHasExternalPackageChange(false)
		, bHasPendingKillChange(false)
		, bHasNonPropertyChanges(false)
	{
	}

	bool HasChanged() const
	{
		return bHasNameChange || bHasOuterChange || bHasExternalPackageChange || bHasPendingKillChange || bHasNonPropertyChanges || ChangedProperties.Num() > 0;
	}

	void Merge(const FTransactionObjectDeltaChange& InOther)
	{
		bHasNameChange |= InOther.bHasNameChange;
		bHasOuterChange |= InOther.bHasOuterChange;
		bHasExternalPackageChange |= InOther.bHasExternalPackageChange;
		bHasPendingKillChange |= InOther.bHasPendingKillChange;
		bHasNonPropertyChanges |= InOther.bHasNonPropertyChanges;

		for (const FName& OtherChangedPropName : InOther.ChangedProperties)
		{
			ChangedProperties.AddUnique(OtherChangedPropName);
		}
	}

	/** True if the object name has changed */
	bool bHasNameChange : 1;
	/** True of the object outer has changed */
	bool bHasOuterChange : 1;
	/** True of the object assigned package has changed */
	bool bHasExternalPackageChange : 1;
	/** True if the object "pending kill" state has changed */
	bool bHasPendingKillChange : 1;
	/** True if the object has changes other than property changes (may be caused by custom serialization) */
	bool bHasNonPropertyChanges : 1;
	/** Array of properties that have changed on the object */
	TArray<FName> ChangedProperties;
};

/** Different kinds of actions that can trigger a transaction object event */
enum class ETransactionObjectEventType : uint8
{
	/** This event was caused by an undo/redo operation */
	UndoRedo,
	/** This event was caused by a transaction being finalized within the transaction system */
	Finalized,
	/** This event was caused by a transaction snapshot. Several of these may be generated in the case of an interactive change */
	Snapshot,
};

/**
 * Transaction object events.
 *
 * Transaction object events are used to notify objects when they are transacted in some way.
 * This mostly just means that an object has had an undo/redo applied to it, however an event is also triggered
 * when the object has been finalized as part of a transaction (allowing you to detect object changes).
 */
class FTransactionObjectEvent
{
public:
	FTransactionObjectEvent() = default;

	FTransactionObjectEvent(const FGuid& InTransactionId, const FGuid& InOperationId, const ETransactionObjectEventType InEventType, const FTransactionObjectDeltaChange& InDeltaChange, const TSharedPtr<ITransactionObjectAnnotation>& InAnnotation
		, const FName InOriginalObjectPackageName, const FName InOriginalObjectName, const FName InOriginalObjectPathName, const FName InOriginalObjectOuterPathName, const FName InOriginalObjectExternalPackageName, const FName InOriginalObjectClassPathName)
		: TransactionId(InTransactionId)
		, OperationId(InOperationId)
		, EventType(InEventType)
		, DeltaChange(InDeltaChange)
		, Annotation(InAnnotation)
		, OriginalObjectPackageName(InOriginalObjectPackageName)
		, OriginalObjectName(InOriginalObjectName)
		, OriginalObjectPathName(InOriginalObjectPathName)
		, OriginalObjectOuterPathName(InOriginalObjectOuterPathName)
		, OriginalObjectExternalPackageName(InOriginalObjectExternalPackageName)
		, OriginalObjectClassPathName(InOriginalObjectClassPathName)
	{
		check(TransactionId.IsValid());
		check(OperationId.IsValid());
	}

	/** The unique identifier of the transaction this event belongs to */
	const FGuid& GetTransactionId() const
	{
		return TransactionId;
	}

	/** The unique identifier for the active operation on the transaction this event belongs to */
	const FGuid& GetOperationId() const
	{
		return OperationId;
	}

	/** What kind of action caused this event? */
	ETransactionObjectEventType GetEventType() const
	{
		return EventType;
	}

	/** Was the pending kill state of this object changed? (implies non-property changes) */
	bool HasPendingKillChange() const
	{
		return DeltaChange.bHasPendingKillChange;
	}

	/** Was the name of this object changed? (implies non-property changes) */
	bool HasNameChange() const
	{
		return DeltaChange.bHasNameChange;
	}

	/** Get the original package name of this object */
	FName GetOriginalObjectPackageName() const
	{
		return OriginalObjectPackageName;
	}

	/** Get the original name of this object */
	FName GetOriginalObjectName() const
	{
		return OriginalObjectName;
	}

	/** Get the original path name of this object */
	FName GetOriginalObjectPathName() const
	{
		return OriginalObjectPathName;
	}

	FName GetOriginalObjectClassPathName() const
	{
		return OriginalObjectClassPathName;
	}

	/** Was the outer of this object changed? (implies non-property changes) */
	bool HasOuterChange() const
	{
		return DeltaChange.bHasOuterChange;
	}

	/** Has the package assigned to this object changed? (implies non-property changes) */
	bool HasExternalPackageChange() const
	{
		return DeltaChange.bHasExternalPackageChange;
	}

	/** Get the original outer path name of this object */
	FName GetOriginalObjectOuterPathName() const
	{
		return OriginalObjectOuterPathName;
	}

	/** Get the original package name of this object */
	FName GetOriginalObjectExternalPackageName() const
	{
		return OriginalObjectExternalPackageName;
	}


	/** Were any non-property changes made to the object? */
	bool HasNonPropertyChanges(const bool InSerializationOnly = false) const
	{
		return (!InSerializationOnly && (DeltaChange.bHasNameChange || DeltaChange.bHasOuterChange || DeltaChange.bHasExternalPackageChange || DeltaChange.bHasPendingKillChange)) || DeltaChange.bHasNonPropertyChanges;
	}

	/** Were any property changes made to the object? */
	bool HasPropertyChanges() const
	{
		return DeltaChange.ChangedProperties.Num() > 0;
	}

	/** Get the list of changed properties. Each entry is actually a chain of property names (root -> leaf) separated by a dot, eg) "ObjProp.StructProp". */
	const TArray<FName>& GetChangedProperties() const
	{
		return DeltaChange.ChangedProperties;
	}

	/** Get the annotation object associated with the object being transacted (if any). */
	TSharedPtr<ITransactionObjectAnnotation> GetAnnotation() const
	{
		return Annotation;
	}

	/** Merge this transaction event with another */
	void Merge(const FTransactionObjectEvent& InOther)
	{
		if (EventType == ETransactionObjectEventType::Snapshot)
		{
			EventType = InOther.EventType;
		}

		DeltaChange.Merge(InOther.DeltaChange);
	}

private:
	FGuid TransactionId;
	FGuid OperationId;
	ETransactionObjectEventType EventType;
	FTransactionObjectDeltaChange DeltaChange;
	TSharedPtr<ITransactionObjectAnnotation> Annotation;
	FName OriginalObjectPackageName;
	FName OriginalObjectName;
	FName OriginalObjectPathName;
	FName OriginalObjectOuterPathName;
	FName OriginalObjectExternalPackageName;
	FName OriginalObjectClassPathName;
};

/**
 * Diff for a given transaction.
 */
struct FTransactionDiff
{
	FGuid TransactionId;
	FString TransactionTitle;
	TMap<FName, TSharedPtr<FTransactionObjectEvent>> DiffMap;
};
