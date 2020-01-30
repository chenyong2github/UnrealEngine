// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertClientTransactionBridge.h"
#include "ConcertLogGlobal.h"
#include "ConcertSyncSettings.h"
#include "ConcertSyncArchives.h"
#include "ConcertSyncClientUtil.h"

#include "Misc/PackageName.h"
#include "Misc/CoreDelegates.h"
#include "HAL/IConsoleManager.h"

#if WITH_EDITOR
	#include "Editor.h"
	#include "UnrealEdGlobals.h"
	#include "Editor/UnrealEdEngine.h"
	#include "Editor/TransBuffer.h"
#endif

#define LOCTEXT_NAMESPACE "ConcertClientTransactionBridge"

namespace ConcertClientTransactionBridgeUtil
{

static TAutoConsoleVariable<int32> CVarIgnoreTransactionIncludeFilter(TEXT("Concert.IgnoreTransactionFilters"), 0, TEXT("Ignore Transaction Object Whitelist Filtering"));

enum class ETransactionFilterResult : uint8
{
	/** Include the object in the Concert Transaction */
	IncludeObject,
	/** Filter the object from the Concert Transaction */
	ExcludeObject,
	/** Filter the entire transaction and prevent propagation */
	ExcludeTransaction,
};

bool RunTransactionFilters(const TArray<FTransactionClassFilter>& InFilters, UObject* InObject)
{
	bool bMatchFilter = false;
	for (const FTransactionClassFilter& TransactionFilter : InFilters)
	{
		UClass* TransactionOuterClass = TransactionFilter.ObjectOuterClass.TryLoadClass<UObject>();
		if (!TransactionOuterClass || InObject->IsInA(TransactionOuterClass))
		{
			for (const FSoftClassPath& ObjectClass : TransactionFilter.ObjectClasses)
			{
				UClass* TransactionClass = ObjectClass.TryLoadClass<UObject>();
				if (TransactionClass && InObject->IsA(TransactionClass))
				{
					bMatchFilter = true;
					break;
				}
			}
		}
	}

	return bMatchFilter;
}

ETransactionFilterResult ApplyTransactionFilters(UObject* InObject, UPackage* InChangedPackage)
{
	// Ignore transient packages and objects, compiled in package are not considered Multi-user content.
	if (!InChangedPackage || InChangedPackage == GetTransientPackage() || InChangedPackage->HasAnyFlags(RF_Transient) || InChangedPackage->HasAnyPackageFlags(PKG_CompiledIn) || InObject->HasAnyFlags(RF_Transient))
	{
		return ETransactionFilterResult::ExcludeObject;
	}

	// Ignore packages outside of known root paths (we ignore read-only roots here to skip things like unsaved worlds)
	if (!FPackageName::IsValidLongPackageName(InChangedPackage->GetName()))
	{
		return ETransactionFilterResult::ExcludeObject;
	}

	const UConcertSyncConfig* SyncConfig = GetDefault<UConcertSyncConfig>();

	// Run our exclude transaction filters: if a filter is matched on an object the whole transaction is excluded.
	if (SyncConfig->ExcludeTransactionClassFilters.Num() > 0 && RunTransactionFilters(SyncConfig->ExcludeTransactionClassFilters, InObject))
	{
		return ETransactionFilterResult::ExcludeTransaction;
	}

	// Run our include object filters: if the list is empty or we actively ignore the list then all objects are included,
	// otherwise a filter needs to be matched.
	if (SyncConfig->IncludeObjectClassFilters.Num() == 0 
		|| (CVarIgnoreTransactionIncludeFilter.GetValueOnAnyThread() > 0)
		|| RunTransactionFilters(SyncConfig->IncludeObjectClassFilters, InObject))
	{
		return ETransactionFilterResult::IncludeObject;
	}

	// Otherwise the object is excluded from the transaction
	return ETransactionFilterResult::ExcludeObject;
}

#if WITH_EDITOR
/** Utility struct to suppress editor transaction notifications and fire the correct delegates */
struct FEditorTransactionNotification
{
	FEditorTransactionNotification(FTransactionContext&& InTransactionContext)
		: TransactionContext(MoveTemp(InTransactionContext))
		, TransBuffer(GUnrealEd ? Cast<UTransBuffer>(GUnrealEd->Trans) : nullptr)
		, bOrigSquelchTransactionNotification(GEditor && GEditor->bSquelchTransactionNotification)
		, bOrigNotifyUndoRedoSelectionChange(GEditor && GEditor->bNotifyUndoRedoSelectionChange)
	{
	}

	void PreUndo()
	{
		if (GEditor)
		{
			GEditor->bSquelchTransactionNotification = true;
			GEditor->bNotifyUndoRedoSelectionChange = true;
			if (TransBuffer)
			{
				TransBuffer->OnBeforeRedoUndo().Broadcast(TransactionContext);
			}
		}
	}

	void PostUndo()
	{
		if (GEditor)
		{
			if (TransBuffer)
			{
				TransBuffer->OnRedo().Broadcast(TransactionContext, true);
			}
			GEditor->bSquelchTransactionNotification = bOrigSquelchTransactionNotification;
			GEditor->bNotifyUndoRedoSelectionChange = bOrigNotifyUndoRedoSelectionChange;
		}
	}

	void HandleObjectTransacted(UObject* InTransactionObject, const FConcertExportedObject& InObjectUpdate, const TSharedPtr<ITransactionObjectAnnotation>& InTransactionAnnotation)
	{
		if (GUnrealEd)
		{
			FTransactionObjectEvent TransactionObjectEvent;
			{
				FTransactionObjectDeltaChange DeltaChange;
				DeltaChange.bHasNameChange = !InObjectUpdate.ObjectData.NewName.IsNone();
				DeltaChange.bHasOuterChange = !InObjectUpdate.ObjectData.NewOuterPathName.IsNone();
				DeltaChange.bHasPendingKillChange = InObjectUpdate.ObjectData.bIsPendingKill != InTransactionObject->IsPendingKill();
				DeltaChange.bHasNonPropertyChanges = InObjectUpdate.ObjectData.SerializedData.Num() > 0;
				for (const FConcertSerializedPropertyData& PropertyData : InObjectUpdate.PropertyDatas)
				{
					DeltaChange.ChangedProperties.Add(PropertyData.PropertyName);
				}
				TransactionObjectEvent = FTransactionObjectEvent(TransactionContext.TransactionId, TransactionContext.OperationId, ETransactionObjectEventType::UndoRedo, DeltaChange, InTransactionAnnotation, InTransactionObject->GetFName(), *InTransactionObject->GetPathName(), InObjectUpdate.ObjectId.ObjectOuterPathName, FName(*InTransactionObject->GetClass()->GetPathName()));
			}
			GUnrealEd->HandleObjectTransacted(InTransactionObject, TransactionObjectEvent);
		}
	}

	FTransactionContext TransactionContext;
	UTransBuffer* TransBuffer;
	bool bOrigSquelchTransactionNotification;
	bool bOrigNotifyUndoRedoSelectionChange;
};
#endif

void ProcessTransactionEvent(const FConcertTransactionEventBase& InEvent, const FConcertSessionVersionInfo* InVersionInfo, const TArray<FName>& InPackagesToProcess, const FConcertLocalIdentifierTable* InLocalIdentifierTablePtr, const bool bIsSnapshot)
{
	// Transactions are applied in multiple-phases...
	//	1) Find or create all objects in the transaction (to handle object-interdependencies in the serialized data)
	//	2) Notify all objects that they are about to be changed (via PreEditUndo)
	//	3) Update the state of all objects
	//	4) Notify all objects that they were changed (via PostEditUndo) - also finish spawning any new actors now that they have the correct state

	// --------------------------------------------------------------------------------------------------------------------
	// Phase 1
	// --------------------------------------------------------------------------------------------------------------------
	bool bObjectsDeleted = false;
	TArray<ConcertSyncClientUtil::FGetObjectResult, TInlineAllocator<8>> TransactionObjects;
	TransactionObjects.AddDefaulted(InEvent.ExportedObjects.Num());
	{
		// Sort the object list so that outers will be created before their child objects
		typedef TTuple<int32, const FConcertExportedObject*> FConcertExportedIndexAndObject;
		TArray<FConcertExportedIndexAndObject, TInlineAllocator<8>> SortedExportedObjects;
		SortedExportedObjects.Reserve(InEvent.ExportedObjects.Num());
		for (int32 ObjectIndex = 0; ObjectIndex < InEvent.ExportedObjects.Num(); ++ObjectIndex)
		{
			SortedExportedObjects.Add(MakeTuple(ObjectIndex, &InEvent.ExportedObjects[ObjectIndex]));
		}

		SortedExportedObjects.StableSort([](const FConcertExportedIndexAndObject& One, const FConcertExportedIndexAndObject& Two) -> bool
		{
			const FConcertExportedObject& OneObjectUpdate = *One.Value;
			const FConcertExportedObject& TwoObjectUpdate = *Two.Value;
			return OneObjectUpdate.ObjectPathDepth < TwoObjectUpdate.ObjectPathDepth;
		});

		// Find or create each object, populating TransactionObjects in the original order (not the sorted order)
		for (const FConcertExportedIndexAndObject& SortedExportedObjectsPair : SortedExportedObjects)
		{
			const int32 ObjectUpdateIndex = SortedExportedObjectsPair.Key;
			const FConcertExportedObject& ObjectUpdate = *SortedExportedObjectsPair.Value;
			ConcertSyncClientUtil::FGetObjectResult& TransactionObjectRef = TransactionObjects[ObjectUpdateIndex];

			// Is this object excluded? We exclude certain packages when re-applying live transactions on a package load
			if (InPackagesToProcess.Num() > 0)
			{
				const FName ObjectOuterPathName = ObjectUpdate.ObjectData.NewOuterPathName.IsNone() ? ObjectUpdate.ObjectId.ObjectOuterPathName : ObjectUpdate.ObjectData.NewOuterPathName;
				const FName ObjectPackageName = *FPackageName::ObjectPathToPackageName(ObjectOuterPathName.ToString());
				if (!InPackagesToProcess.Contains(ObjectPackageName))
				{
					continue;
				}
			}

			// Find or create the object
			TransactionObjectRef = ConcertSyncClientUtil::GetObject(ObjectUpdate.ObjectId, ObjectUpdate.ObjectData.NewName, ObjectUpdate.ObjectData.NewOuterPathName, ObjectUpdate.ObjectData.bAllowCreate);
			bObjectsDeleted |= (ObjectUpdate.ObjectData.bIsPendingKill || TransactionObjectRef.NeedsGC());
		}
	}

#if WITH_EDITOR
	UObject* PrimaryObject = InEvent.PrimaryObjectId.ObjectName.IsNone() ? nullptr : ConcertSyncClientUtil::GetObject(InEvent.PrimaryObjectId, FName(), FName(), /*bAllowCreate*/false).Obj;
	FEditorTransactionNotification EditorTransactionNotification(FTransactionContext(InEvent.TransactionId, InEvent.OperationId, LOCTEXT("ConcertTransactionEvent", "Concert Transaction Event"), TEXT("Concert Transaction Event"), PrimaryObject));
	if (!bIsSnapshot)
	{
		EditorTransactionNotification.PreUndo();
	}
#endif

	// --------------------------------------------------------------------------------------------------------------------
	// Phase 2
	// --------------------------------------------------------------------------------------------------------------------
#if WITH_EDITOR
	TArray<TSharedPtr<ITransactionObjectAnnotation>, TInlineAllocator<8>> TransactionAnnotations;
	TransactionAnnotations.AddDefaulted(InEvent.ExportedObjects.Num());
	for (int32 ObjectIndex = 0; ObjectIndex < TransactionObjects.Num(); ++ObjectIndex)
	{
		const ConcertSyncClientUtil::FGetObjectResult& TransactionObjectRef = TransactionObjects[ObjectIndex];
		const FConcertExportedObject& ObjectUpdate = InEvent.ExportedObjects[ObjectIndex];

		UObject* TransactionObject = TransactionObjectRef.Obj;
		if (!TransactionObject)
		{
			continue;
		}

		// Restore its annotation data
		TSharedPtr<ITransactionObjectAnnotation>& TransactionAnnotation = TransactionAnnotations[ObjectIndex];
		if (ObjectUpdate.SerializedAnnotationData.Num() > 0)
		{
			FConcertSyncObjectReader AnnotationReader(InLocalIdentifierTablePtr, FConcertSyncWorldRemapper(), InVersionInfo, TransactionObject, ObjectUpdate.SerializedAnnotationData);
			TransactionAnnotation = TransactionObject->CreateAndRestoreTransactionAnnotation(AnnotationReader);
			UE_CLOG(!TransactionAnnotation.IsValid(), LogConcert, Warning, TEXT("Object '%s' had transaction annotation data that failed to restore!"), *TransactionObject->GetPathName());
		}

		// Notify before changing anything
		if (!bIsSnapshot || TransactionAnnotation)
		{
			// Transaction annotations require us to invoke the redo flow (even for snapshots!) as that's the only thing that can apply the annotation
			TransactionObject->PreEditUndo();
		}

		// We need to manually call OnPreObjectPropertyChanged as PreEditUndo calls the PreEditChange version that skips it, but we have things that rely on it being called
		// For snapshot events this also triggers PreEditChange directly since we can skip the call to PreEditUndo
		for (const FConcertSerializedPropertyData& PropertyData : ObjectUpdate.PropertyDatas)
		{
			FProperty* TransactionProp = FindField<FProperty>(TransactionObject->GetClass(), PropertyData.PropertyName);
			if (TransactionProp)
			{
				if (bIsSnapshot)
				{
					TransactionObject->PreEditChange(TransactionProp);
				}

				FEditPropertyChain PropertyChain;
				PropertyChain.AddHead(TransactionProp);
				FCoreUObjectDelegates::OnPreObjectPropertyChanged.Broadcast(TransactionObject, PropertyChain);
			}
		}
	}
#endif

	// --------------------------------------------------------------------------------------------------------------------
	// Phase 3
	// --------------------------------------------------------------------------------------------------------------------
	for (int32 ObjectIndex = 0; ObjectIndex < TransactionObjects.Num(); ++ObjectIndex)
	{
		const ConcertSyncClientUtil::FGetObjectResult& TransactionObjectRef = TransactionObjects[ObjectIndex];
		const FConcertExportedObject& ObjectUpdate = InEvent.ExportedObjects[ObjectIndex];

		UObject* TransactionObject = TransactionObjectRef.Obj;
		if (!TransactionObject)
		{
			continue;
		}

		// Update the pending kill state
		ConcertSyncClientUtil::UpdatePendingKillState(TransactionObject, ObjectUpdate.ObjectData.bIsPendingKill);

		// Apply the new data
		if (ObjectUpdate.ObjectData.SerializedData.Num() > 0)
		{
			FConcertSyncObjectReader ObjectReader(InLocalIdentifierTablePtr, FConcertSyncWorldRemapper(), InVersionInfo, TransactionObject, ObjectUpdate.ObjectData.SerializedData);
			ObjectReader.SerializeObject(TransactionObject);
		}
		else
		{
			for (const FConcertSerializedPropertyData& PropertyData : ObjectUpdate.PropertyDatas)
			{
				FProperty* TransactionProp = FindField<FProperty>(TransactionObject->GetClass(), PropertyData.PropertyName);
				if (TransactionProp)
				{
					FConcertSyncObjectReader ObjectReader(InLocalIdentifierTablePtr, FConcertSyncWorldRemapper(), InVersionInfo, TransactionObject, PropertyData.SerializedData);
					ObjectReader.SerializeProperty(TransactionProp, TransactionObject);
				}
			}
		}
	}

	// --------------------------------------------------------------------------------------------------------------------
	// Phase 4
	// --------------------------------------------------------------------------------------------------------------------
	for (int32 ObjectIndex = 0; ObjectIndex < TransactionObjects.Num(); ++ObjectIndex)
	{
		const ConcertSyncClientUtil::FGetObjectResult& TransactionObjectRef = TransactionObjects[ObjectIndex];
		const FConcertExportedObject& ObjectUpdate = InEvent.ExportedObjects[ObjectIndex];

		UObject* TransactionObject = TransactionObjectRef.Obj;
		if (!TransactionObject)
		{
			continue;
		}

		// Finish spawning any newly created actors
		if (TransactionObjectRef.NeedsPostSpawn())
		{
			AActor* TransactionActor = CastChecked<AActor>(TransactionObject);
			TransactionActor->FinishSpawning(FTransform(), true);
		}

#if WITH_EDITOR
		// We need to manually call OnObjectPropertyChanged as PostEditUndo calls the PostEditChange version that skips it, but we have things that rely on it being called
		// For snapshot events this also triggers PostEditChange directly since we can skip the call to PostEditUndo
		for (const FConcertSerializedPropertyData& PropertyData : ObjectUpdate.PropertyDatas)
		{
			FProperty* TransactionProp = FindField<FProperty>(TransactionObject->GetClass(), PropertyData.PropertyName);
			if (TransactionProp)
			{
				if (bIsSnapshot)
				{
					TransactionObject->PostEditChange();
				}

				FPropertyChangedEvent PropertyChangedEvent(TransactionProp, bIsSnapshot ? EPropertyChangeType::Interactive : EPropertyChangeType::Unspecified);
				FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(TransactionObject, PropertyChangedEvent);
			}
		}

		// Notify after changing everything
		const TSharedPtr<ITransactionObjectAnnotation>& TransactionAnnotation = TransactionAnnotations[ObjectIndex];
		if (TransactionAnnotation)
		{
			// Transaction annotations require us to invoke the redo flow (even for snapshots!) as that's the only thing that can apply the annotation
			TransactionObject->PostEditUndo(TransactionAnnotation);
		}
		else if (!bIsSnapshot)
		{
			TransactionObject->PostEditUndo();
		}

		// Notify the editor that a transaction happened, as some things rely on this being called
		// We need to call this ourselves as we aren't actually going through the full transaction redo that the editor hooks in to to generate these notifications
		if (!bIsSnapshot)
		{
			EditorTransactionNotification.HandleObjectTransacted(TransactionObject, ObjectUpdate, TransactionAnnotation);
		}
#endif
	}

#if WITH_EDITOR
	if (!bIsSnapshot)
	{
		EditorTransactionNotification.PostUndo();
	}
#endif

	// TODO: This can sometimes cause deadlocks - need to investigate why
	if (bObjectsDeleted)
	{
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, false);
	}

#if WITH_EDITOR
	if (bIsSnapshot && GUnrealEd)
	{
		GUnrealEd->UpdatePivotLocationForSelection();
	}
#endif
}

}	// namespace ConcertClientTransactionBridgeUtil

FConcertClientTransactionBridge::FConcertClientTransactionBridge()
	: bHasBoundUnderlyingLocalTransactionEvents(false)
	, bIgnoreLocalTransactions(false)
{
	ConditionalBindUnderlyingLocalTransactionEvents();

	FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(this, &FConcertClientTransactionBridge::OnEngineInitComplete);
	FCoreDelegates::OnEndFrame.AddRaw(this, &FConcertClientTransactionBridge::OnEndFrame);
}

FConcertClientTransactionBridge::~FConcertClientTransactionBridge()
{
#if WITH_EDITOR
	// Unregister Object Transaction events
	if (GUnrealEd)
	{
		if (UTransBuffer* TransBuffer = Cast<UTransBuffer>(GUnrealEd->Trans))
		{
			TransBuffer->OnTransactionStateChanged().RemoveAll(this);
		}
	}
	FCoreUObjectDelegates::OnObjectTransacted.RemoveAll(this);
#endif

	FCoreDelegates::OnFEngineLoopInitComplete.RemoveAll(this);
	FCoreDelegates::OnEndFrame.RemoveAll(this);
}

FOnConcertClientLocalTransactionSnapshot& FConcertClientTransactionBridge::OnLocalTransactionSnapshot()
{
	return OnLocalTransactionSnapshotDelegate;
}

FOnConcertClientLocalTransactionFinalized& FConcertClientTransactionBridge::OnLocalTransactionFinalized()
{
	return OnLocalTransactionFinalizedDelegate;
}

bool FConcertClientTransactionBridge::CanApplyRemoteTransaction() const
{
	return ConcertSyncClientUtil::CanPerformBlockingAction();
}

void FConcertClientTransactionBridge::ApplyRemoteTransaction(const FConcertTransactionEventBase& InEvent, const FConcertSessionVersionInfo* InVersionInfo, const TArray<FName>& InPackagesToProcess, const FConcertLocalIdentifierTable* InLocalIdentifierTablePtr, const bool bIsSnapshot)
{
	ConcertClientTransactionBridgeUtil::ProcessTransactionEvent(InEvent, InVersionInfo, InPackagesToProcess, InLocalIdentifierTablePtr, bIsSnapshot);
}

bool& FConcertClientTransactionBridge::GetIgnoreLocalTransactionsRef()
{
	return bIgnoreLocalTransactions;
}

void FConcertClientTransactionBridge::HandleTransactionStateChanged(const FTransactionContext& InTransactionContext, const ETransactionStateEventType InTransactionState)
{
	if (bIgnoreLocalTransactions)
	{
		return;
	}

	{
		const TCHAR* TransactionStateString = TEXT("");
		switch (InTransactionState)
		{
#define ENUM_TO_STRING(ENUM)						\
		case ETransactionStateEventType::ENUM:		\
			TransactionStateString = TEXT(#ENUM);	\
				break;
		ENUM_TO_STRING(TransactionStarted)
		ENUM_TO_STRING(TransactionCanceled)
		ENUM_TO_STRING(TransactionFinalized)
		ENUM_TO_STRING(UndoRedoStarted)
		ENUM_TO_STRING(UndoRedoFinalized)
#undef ENUM_TO_STRING
		default:
			break;
		}

		UE_LOG(LogConcert, VeryVerbose, TEXT("Transaction %s (%s): %s"), *InTransactionContext.TransactionId.ToString(), *InTransactionContext.OperationId.ToString(), TransactionStateString);
	}

	// Create, finalize, or remove an ongoing transaction
	if (InTransactionState == ETransactionStateEventType::TransactionStarted || InTransactionState == ETransactionStateEventType::UndoRedoStarted)
	{
		// Start a new ongoing transaction
		check(!OngoingTransactions.Contains(InTransactionContext.OperationId));
		OngoingTransactionsOrder.Add(InTransactionContext.OperationId);
		OngoingTransactions.Add(InTransactionContext.OperationId, FOngoingTransaction(InTransactionContext.Title, InTransactionContext.TransactionId, InTransactionContext.OperationId, InTransactionContext.PrimaryObject));
	}
	else if (InTransactionState == ETransactionStateEventType::TransactionFinalized || InTransactionState == ETransactionStateEventType::UndoRedoFinalized)
	{
		// Finalize an existing ongoing transaction
		FOngoingTransaction& OngoingTransaction = OngoingTransactions.FindChecked(InTransactionContext.OperationId);
		OngoingTransaction.CommonData.TransactionTitle = InTransactionContext.Title;
		OngoingTransaction.CommonData.PrimaryObject = InTransactionContext.PrimaryObject;
		OngoingTransaction.bIsFinalized = true;
	}
	else if (InTransactionState == ETransactionStateEventType::TransactionCanceled)
	{
		// We receive an object undo event before a transaction is canceled to restore the object to its original state
		// We need to keep this update if we notified of any snapshots for this transaction (to undo the snapshot changes), otherwise we can just drop this transaction as no changes have notified
		FOngoingTransaction& OngoingTransaction = OngoingTransactions.FindChecked(InTransactionContext.OperationId);
		if (OngoingTransaction.bHasNotifiedSnapshot)
		{
			// Finalize the transaction
			OngoingTransaction.CommonData.TransactionTitle = InTransactionContext.Title;
			OngoingTransaction.CommonData.PrimaryObject = InTransactionContext.PrimaryObject;
			OngoingTransaction.bIsFinalized = true;
			OngoingTransaction.FinalizedData.bWasCanceled = true;
		}
		else
		{
			// Note: We don't remove this from OngoingTransactionsOrder as we just skip transactions missing from the map (assuming they've been canceled).
			OngoingTransactions.Remove(InTransactionContext.OperationId);
		}
	}
}

void FConcertClientTransactionBridge::HandleObjectTransacted(UObject* InObject, const FTransactionObjectEvent& InTransactionEvent)
{
	if (bIgnoreLocalTransactions)
	{
		return;
	}

	UPackage* ChangedPackage = InObject->GetOutermost();
	ConcertClientTransactionBridgeUtil::ETransactionFilterResult FilterResult = ConcertClientTransactionBridgeUtil::ApplyTransactionFilters(InObject, ChangedPackage);
	FOngoingTransaction* TrackedTransaction = OngoingTransactions.Find(InTransactionEvent.GetOperationId());

	// TODO: This needs to send both editor-only and non-editor-only payload data to the server, which will forward only the correct part to cooked and non-cooked clients
	bool bIncludeEditorOnlyProperties = true;

	{
		const TCHAR* ObjectEventString = TEXT("");
		switch (InTransactionEvent.GetEventType())
		{
#define ENUM_TO_STRING(ENUM)						\
		case ETransactionObjectEventType::ENUM:		\
			ObjectEventString = TEXT(#ENUM);		\
				break;
		ENUM_TO_STRING(UndoRedo)
		ENUM_TO_STRING(Finalized)
		ENUM_TO_STRING(Snapshot)
#undef ENUM_TO_STRING
		default:
			break;
		}

		UE_LOG(LogConcert, VeryVerbose,
			TEXT("%s Transaction %s (%s, %s):%s %s:%s (%s property changes, %s object changes)"), 
			(TrackedTransaction != nullptr ? TEXT("Tracked") : TEXT("Untracked")),
			*InTransactionEvent.GetTransactionId().ToString(),
			*InTransactionEvent.GetOperationId().ToString(),
			ObjectEventString,
			(FilterResult == ConcertClientTransactionBridgeUtil::ETransactionFilterResult::ExcludeObject ? TEXT(" FILTERED OBJECT: ") : TEXT("")),
			*InObject->GetClass()->GetName(),
			*InObject->GetPathName(), 
			(InTransactionEvent.HasPropertyChanges() ? TEXT("has") : TEXT("no")), 
			(InTransactionEvent.HasNonPropertyChanges() ? TEXT("has") : TEXT("no"))
			);
	}

	if (TrackedTransaction == nullptr)
	{
		return;
	}

	const FConcertObjectId ObjectId = FConcertObjectId(*InObject->GetClass()->GetPathName(), InTransactionEvent.GetOriginalObjectOuterPathName(), InTransactionEvent.GetOriginalObjectName(), InObject->GetFlags());
	FOngoingTransaction& OngoingTransaction = *TrackedTransaction;

	// If the object is excluded or exclude the whole transaction add it to the excluded list
	if (FilterResult != ConcertClientTransactionBridgeUtil::ETransactionFilterResult::IncludeObject)
	{
		OngoingTransaction.CommonData.bIsExcluded |= FilterResult == ConcertClientTransactionBridgeUtil::ETransactionFilterResult::ExcludeTransaction;
		OngoingTransaction.CommonData.ExcludedObjectUpdates.Add(ObjectId);
		return;
	}

	const FName NewObjectName = InTransactionEvent.HasNameChange() ? InObject->GetFName() : FName();
	const FName NewObjectOuterPathName = (InTransactionEvent.HasOuterChange() && InObject->GetOuter()) ? FName(*InObject->GetOuter()->GetPathName()) : FName();
	const TArray<FName> RootPropertyNames = ConcertSyncClientUtil::GetRootProperties(InTransactionEvent.GetChangedProperties());
	TSharedPtr<ITransactionObjectAnnotation> TransactionAnnotation = InTransactionEvent.GetAnnotation();

	// Track which packages were changed
	OngoingTransaction.CommonData.ModifiedPackages.AddUnique(ChangedPackage->GetFName());

	// Add this object change to its pending transaction
	if (InTransactionEvent.GetEventType() == ETransactionObjectEventType::Snapshot)
	{
		// Merge the snapshot property changes into pending snapshot list
		if (OnLocalTransactionSnapshotDelegate.IsBound() && (InTransactionEvent.HasPropertyChanges() || TransactionAnnotation.IsValid()))
		{
			// Find or add an entry for this object
			FConcertExportedObject* ObjectUpdatePtr = OngoingTransaction.SnapshotData.SnapshotObjectUpdates.FindByPredicate([&ObjectId](FConcertExportedObject& ObjectUpdate)
			{
				return ConcertSyncClientUtil::ObjectIdsMatch(ObjectId, ObjectUpdate.ObjectId);
			});
			if (!ObjectUpdatePtr)
			{
				ObjectUpdatePtr = &OngoingTransaction.SnapshotData.SnapshotObjectUpdates.AddDefaulted_GetRef();
				ObjectUpdatePtr->ObjectId = ObjectId;
				ObjectUpdatePtr->ObjectPathDepth = ConcertSyncClientUtil::GetObjectPathDepth(InObject);
				ObjectUpdatePtr->ObjectData.bAllowCreate = false;
				ObjectUpdatePtr->ObjectData.bIsPendingKill = InObject->IsPendingKill();
			}

			if (TransactionAnnotation.IsValid())
			{
				ObjectUpdatePtr->SerializedAnnotationData.Reset();
				FConcertSyncObjectWriter AnnotationWriter(nullptr, InObject, ObjectUpdatePtr->SerializedAnnotationData, bIncludeEditorOnlyProperties, true);
				TransactionAnnotation->Serialize(AnnotationWriter);
			}

			// Find or add an update for each property
			for (const FName& RootPropertyName : RootPropertyNames)
			{
				FProperty* RootProperty = ConcertSyncClientUtil::GetExportedProperty(InObject->GetClass(), RootPropertyName, bIncludeEditorOnlyProperties);
				if (!RootProperty)
				{
					continue;
				}

				FConcertSerializedPropertyData* PropertyDataPtr = ObjectUpdatePtr->PropertyDatas.FindByPredicate([&RootPropertyName](FConcertSerializedPropertyData& PropertyData)
				{
					return RootPropertyName == PropertyData.PropertyName;
				});
				if (!PropertyDataPtr)
				{
					PropertyDataPtr = &ObjectUpdatePtr->PropertyDatas.AddDefaulted_GetRef();
					PropertyDataPtr->PropertyName = RootPropertyName;
				}

				PropertyDataPtr->SerializedData.Reset();
				ConcertSyncClientUtil::SerializeProperty(nullptr, InObject, RootProperty, bIncludeEditorOnlyProperties, PropertyDataPtr->SerializedData);
			}
		}
	}
	else if (OnLocalTransactionFinalizedDelegate.IsBound())
	{
		FConcertExportedObject& ObjectUpdate = OngoingTransaction.FinalizedData.FinalizedObjectUpdates.AddDefaulted_GetRef();
		ObjectUpdate.ObjectId = ObjectId;
		ObjectUpdate.ObjectPathDepth = ConcertSyncClientUtil::GetObjectPathDepth(InObject);
		ObjectUpdate.ObjectData.bAllowCreate = InTransactionEvent.HasPendingKillChange() && !InObject->IsPendingKill();
		ObjectUpdate.ObjectData.bIsPendingKill = InObject->IsPendingKill();
		ObjectUpdate.ObjectData.NewName = NewObjectName;
		ObjectUpdate.ObjectData.NewOuterPathName = NewObjectOuterPathName;

		if (TransactionAnnotation.IsValid())
		{
			FConcertSyncObjectWriter AnnotationWriter(&OngoingTransaction.FinalizedData.FinalizedLocalIdentifierTable, InObject, ObjectUpdate.SerializedAnnotationData, bIncludeEditorOnlyProperties, false);
			TransactionAnnotation->Serialize(AnnotationWriter);
		}

		// If this object changed from being pending kill to not being pending kill, we have to send a full object update (including all properties), rather than attempt a delta-update
		const bool bForceFullObjectUpdate = InTransactionEvent.HasPendingKillChange() && !InObject->IsPendingKill();
		if (bForceFullObjectUpdate)
		{
			// Serialize the entire object.
			ConcertSyncClientUtil::SerializeObject(&OngoingTransaction.FinalizedData.FinalizedLocalIdentifierTable, InObject, nullptr, bIncludeEditorOnlyProperties, ObjectUpdate.ObjectData.SerializedData);
		}
		else if (InTransactionEvent.HasNonPropertyChanges(/*SerializationOnly*/true))
		{
			// The 'non-property changes' refers to custom data added by a deriver UObject before and/or after the standard serialized data. Since this is a custom
			// data format, we don't know what changed, call the object to re-serialize this part, but still send the delta for the generic reflected properties (in RootPropertyNames).
			ConcertSyncClientUtil::SerializeObject(&OngoingTransaction.FinalizedData.FinalizedLocalIdentifierTable, InObject, &RootPropertyNames, bIncludeEditorOnlyProperties, ObjectUpdate.ObjectData.SerializedData);

			// Track which properties changed. Not used to apply the transaction on the receiving side, the object specific serialization function will be used for that, but
			// to be able to display, in the transaction detail view, which 'properties' changed in the transaction as transaction data is otherwise opaque to UI.
			for (const FName& RootPropertyName : RootPropertyNames)
			{
				if (FProperty* RootProperty = ConcertSyncClientUtil::GetExportedProperty(InObject->GetClass(), RootPropertyName, bIncludeEditorOnlyProperties))
				{
					FConcertSerializedPropertyData& PropertyData = ObjectUpdate.PropertyDatas.AddDefaulted_GetRef();
					PropertyData.PropertyName = RootPropertyName;
				}
			}
		}
		else // Its possible to optimize the transaction payload, only sending a 'delta' update.
		{
			// Only send properties that changed. The receiving side will 'patch' the object using the reflection system. The specific object serialization function will NOT be called.
			for (const FName& RootPropertyName : RootPropertyNames)
			{
				if (FProperty* RootProperty = ConcertSyncClientUtil::GetExportedProperty(InObject->GetClass(), RootPropertyName, bIncludeEditorOnlyProperties))
				{
					FConcertSerializedPropertyData& PropertyData = ObjectUpdate.PropertyDatas.AddDefaulted_GetRef();
					PropertyData.PropertyName = RootPropertyName;
					ConcertSyncClientUtil::SerializeProperty(&OngoingTransaction.FinalizedData.FinalizedLocalIdentifierTable, InObject, RootProperty, bIncludeEditorOnlyProperties, PropertyData.SerializedData);
				}
			}
		}
	}
}

void FConcertClientTransactionBridge::ConditionalBindUnderlyingLocalTransactionEvents()
{
	if (bHasBoundUnderlyingLocalTransactionEvents)
	{
		return;
	}

#if WITH_EDITOR
	// If the bridge is created while a transaction is ongoing, add it as pending
	if (GUndo)
	{
		// Start a new pending transaction
		HandleTransactionStateChanged(GUndo->GetContext(), ETransactionStateEventType::TransactionStarted);
	}

	// Register Object Transaction events
	if (GUnrealEd)
	{
		if (UTransBuffer* TransBuffer = Cast<UTransBuffer>(GUnrealEd->Trans))
		{
			bHasBoundUnderlyingLocalTransactionEvents = true;
			TransBuffer->OnTransactionStateChanged().AddRaw(this, &FConcertClientTransactionBridge::HandleTransactionStateChanged);
			FCoreUObjectDelegates::OnObjectTransacted.AddRaw(this, &FConcertClientTransactionBridge::HandleObjectTransacted);
		}
	}
#endif
}

void FConcertClientTransactionBridge::OnEngineInitComplete()
{
	ConditionalBindUnderlyingLocalTransactionEvents();
}

void FConcertClientTransactionBridge::OnEndFrame()
{
	for (auto OngoingTransactionsOrderIter = OngoingTransactionsOrder.CreateIterator(); OngoingTransactionsOrderIter; ++OngoingTransactionsOrderIter)
	{
		FOngoingTransaction* OngoingTransactionPtr = OngoingTransactions.Find(*OngoingTransactionsOrderIter);
		if (!OngoingTransactionPtr)
		{
			// Missing transaction, must have been canceled...
			OngoingTransactionsOrderIter.RemoveCurrent();
			continue;
		}

		if (OngoingTransactionPtr->bIsFinalized)
		{
			OnLocalTransactionFinalizedDelegate.Broadcast(OngoingTransactionPtr->CommonData, OngoingTransactionPtr->FinalizedData);

			OngoingTransactions.Remove(OngoingTransactionPtr->CommonData.OperationId);
			OngoingTransactionsOrderIter.RemoveCurrent();
			continue;
		}
		else if (OngoingTransactionPtr->SnapshotData.SnapshotObjectUpdates.Num() > 0)
		{
			OnLocalTransactionSnapshotDelegate.Broadcast(OngoingTransactionPtr->CommonData, OngoingTransactionPtr->SnapshotData);

			OngoingTransactionPtr->bHasNotifiedSnapshot = true;
			OngoingTransactionPtr->SnapshotData.SnapshotObjectUpdates.Reset();
		}
	}
}

#undef LOCTEXT_NAMESPACE
