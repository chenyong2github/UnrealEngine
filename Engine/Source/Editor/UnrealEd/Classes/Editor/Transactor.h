// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Base class for tracking transactions for undo/redo.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Package.h"
#include "UObject/Object.h"
#include "UObject/Package.h"
#include "Serialization/ArchiveUObject.h"
#include "Serialization/ArchiveSerializedPropertyChain.h"
#include "Misc/ITransaction.h"
#include "Algo/Find.h"
#include <type_traits>
#include "Transactor.generated.h"

/*-----------------------------------------------------------------------------
	FTransaction.
-----------------------------------------------------------------------------*/

/**
 * A single transaction, representing a set of serialized, undo-able changes to a set of objects.
 *
 * warning: The undo buffer cannot be made persistent because of its dependence on offsets
 * of arrays from their owning UObjects.
 *
 * warning: UObject::Serialize implicitly assumes that class properties do not change
 * in between transaction resets.
 */
class UNREALED_API FTransaction : public ITransaction
{
	friend class FTransactionObjectEvent;

protected:
	/**
		This type is necessary because the blueprint system is destroying and creating
		CDOs at edit time (usually on compile, but also on load), but also stores user
		entered data in the CDO. We "need"  changes to a CDO to persist across instances
		because as we undo and redo we  need to apply changes to different instances of
		the CDO - alternatively we could destroy and create the CDO as part of a transaction
		(this alternative is the reason for the bunny ears around need).

		DanO: My long term preference is for the editor to use a dynamic, mutable type
		(rather than the CDO) to store editor data. The CDO can then be re-instanced (or not)
		as runtime code requires.
	*/
	struct FPersistentObjectRef
	{
	public:
		friend FArchive& operator<<(FArchive& Ar, FPersistentObjectRef& ReferencedObject)
		{
			Ar << (std::underlying_type_t<EReferenceType>&)ReferencedObject.ReferenceType;
			Ar << ReferencedObject.RootObject;
			Ar << ReferencedObject.SubObjectHierarchyIDs;
			return Ar;
		}

		friend bool operator==(const FPersistentObjectRef& LHS, const FPersistentObjectRef& RHS)
		{
			return LHS.ReferenceType == RHS.ReferenceType
				&& LHS.RootObject == RHS.RootObject
				&& (LHS.ReferenceType != EReferenceType::SubObject || LHS.SubObjectHierarchyIDs == RHS.SubObjectHierarchyIDs);
		}

		friend bool operator!=(const FPersistentObjectRef& LHS, const FPersistentObjectRef& RHS)
		{
			return !(LHS == RHS);
		}

		friend uint32 GetTypeHash(const FPersistentObjectRef& InObjRef)
		{
			return HashCombine(GetTypeHash(InObjRef.ReferenceType), GetTypeHash(InObjRef.RootObject));
		}

		FPersistentObjectRef() = default;
		explicit FPersistentObjectRef(UObject* InObject);

		bool IsRootObjectReference() const
		{
			return ReferenceType == EReferenceType::RootObject;
		}

		bool IsSubObjectReference() const
		{
			return ReferenceType == EReferenceType::SubObject;
		}

		UObject* Get() const;

		void AddReferencedObjects(FReferenceCollector& Collector);

	private:
		/** This enum represents all of the different special cases we are handling with this type */
		enum class EReferenceType : uint8
		{
			Unknown,
			RootObject,
			SubObject,
		};

		/** The reference type we're handling */
		EReferenceType ReferenceType = EReferenceType::Unknown;
		/** Stores the object pointer when ReferenceType==RootObject, and the outermost pointer of the sub-object chain when when ReferenceType==SubObject */
		TObjectPtr<UObject> RootObject = nullptr;
		/** Stores the sub-object name chain when ReferenceType==SubObject */
		TArray<FName, TInlineAllocator<4>> SubObjectHierarchyIDs;

		/** Cached pointers corresponding to RootObject when ReferenceType==SubObject (@note cache needs testing on access as it may have become stale) */
		mutable TWeakObjectPtr<UObject> CachedRootObject;
		/** Cache of pointers corresponding to the items within SubObjectHierarchyIDs when ReferenceType==SubObject (@note cache needs testing on access as it may have become stale) */
		mutable TArray<TWeakObjectPtr<UObject>, TInlineAllocator<4>> CachedSubObjectHierarchy;
	};

	// Record of an object.
	class UNREALED_API FObjectRecord
	{
	public:
		struct FSerializedProperty
		{
			FSerializedProperty()
				: DataOffset(INDEX_NONE)
				, DataSize(0)
			{
			}

			static FName BuildSerializedPropertyKey(const FArchiveSerializedPropertyChain& InPropertyChain)
			{
				const int32 NumProperties = InPropertyChain.GetNumProperties();
				check(NumProperties > 0);
				return InPropertyChain.GetPropertyFromRoot(0)->GetFName();
			}

			void AppendSerializedData(const int64 InOffset, const int64 InSize)
			{
				if (DataOffset == INDEX_NONE)
				{
					DataOffset = InOffset;
					DataSize = InSize;
				}
				else
				{
					DataOffset = FMath::Min(DataOffset, InOffset);
					DataSize = FMath::Max(InOffset + InSize - DataOffset, DataSize);
				}
			}

			/** Offset to the start of this property within the serialized object */
			int64 DataOffset;
			/** Size (in bytes) of this property within the serialized object */
			int64 DataSize;
		};

		struct FSerializedObject
		{
			FSerializedObject()
				: bIsPendingKill(false)
			{
			}

			void SetObject(const UObject* InObject)
			{
				ObjectPackageName = InObject->GetPackage()->GetFName();
				ObjectName = InObject->GetFName();
				ObjectPathName = *InObject->GetPathName();
				ObjectOuterPathName = InObject->GetOuter() ? FName(*InObject->GetOuter()->GetPathName()) : FName();
				ObjectExternalPackageName = InObject->GetExternalPackage() ? InObject->GetExternalPackage()->GetFName() : FName();
				ObjectClassPathName = FName(*InObject->GetClass()->GetPathName());
				bIsPendingKill = IsValid(InObject);
				ObjectAnnotation = InObject->FindOrCreateTransactionAnnotation();
			}

			void Reset()
			{
				ObjectPackageName = FName();
				ObjectName = FName();
				ObjectPathName = FName();
				ObjectOuterPathName = FName();
				ObjectExternalPackageName = FName();
				ObjectClassPathName = FName();
				bIsPendingKill = false;
				Data.Reset();
				ReferencedObjects.Reset();
				ReferencedNames.Reset();
				SerializedProperties.Reset();
				SerializedObjectIndices.Reset();
				SerializedNameIndices.Reset();
				ObjectAnnotation.Reset();
			}

			void Swap(FSerializedObject& Other)
			{
				Exchange(ObjectPackageName, Other.ObjectPackageName);
				Exchange(ObjectName, Other.ObjectName);
				Exchange(ObjectPathName, Other.ObjectPathName);
				Exchange(ObjectOuterPathName, Other.ObjectOuterPathName);
				Exchange(ObjectExternalPackageName, Other.ObjectExternalPackageName);
				Exchange(ObjectClassPathName, Other.ObjectClassPathName);
				Exchange(bIsPendingKill, Other.bIsPendingKill);
				Exchange(Data, Other.Data);
				Exchange(ReferencedObjects, Other.ReferencedObjects);
				Exchange(ReferencedNames, Other.ReferencedNames);
				Exchange(SerializedProperties, Other.SerializedProperties);
				Exchange(SerializedObjectIndices, Other.SerializedObjectIndices);
				Exchange(SerializedNameIndices, Other.SerializedNameIndices);
				Exchange(ObjectAnnotation, Other.ObjectAnnotation);
			}

			/** The package name of the object when it was serialized, can be dictated either by outer chain or external package */
			FName ObjectPackageName;
			/** The name of the object when it was serialized */
			FName ObjectName;
			/** The path name of the object when it was serialized */
			FName ObjectPathName;
			/** The outer path name of the object when it was serialized */
			FName ObjectOuterPathName;
			/** The external package name of the object when it was serialized, if any */
			FName ObjectExternalPackageName;
			/** The path name of the object's class. */
			FName ObjectClassPathName;
			/** The pending kill state of the object when it was serialized */
			bool bIsPendingKill;
			/** The data stream used to serialize/deserialize record */
			TArray64<uint8> Data;
			/** External objects referenced in the transaction */
			TArray<FPersistentObjectRef> ReferencedObjects;
			/** FNames referenced in the object record */
			TArray<FName> ReferencedNames;
			/** Information about the properties that were serialized within this object */
			TMap<FName, FSerializedProperty> SerializedProperties;
			/** Information about the object pointer offsets that were serialized within this object (this maps the property name (or None if there was no property) to the ReferencedObjects indices of the property) */
			TMultiMap<FName, int32> SerializedObjectIndices;
			/** Information about the name offsets that were serialized within this object (this maps the property name to the ReferencedNames index of the property) */
			TMultiMap<FName, int32> SerializedNameIndices;
			/** Annotation data for the object stored externally */
			TSharedPtr<ITransactionObjectAnnotation> ObjectAnnotation;
		};

		// Variables.
		/** The object to track */
		FPersistentObjectRef Object;
		/** Custom change to apply to this object to undo this record.  Executing the undo will return an object that can be used to redo the change. */
		TUniquePtr<FChange> CustomChange;
		/** Array: If an array object, reference to script array */
		FScriptArray*		Array = nullptr;
		/** Array: Offset into the array */
		int32				Index = 0;
		/** Array: How many items to record */
		int32				Count = 0;
		/** @todo document this
		 Array: Operation performed on array: 1 (add/insert), 0 (modify), -1 (remove)? */
		int32				Oper = 0;
		/** Array: Size of each item in the array */
		int32				ElementSize = 0;
		/** Array: Alignment of each item in the array */
		uint32				ElementAlignment = 0;
		/** Array: DefaultConstructor for each item in the array */
		STRUCT_DC			DefaultConstructor;
		/** Array: Serializer to use for each item in the array */
		STRUCT_AR			Serializer;
		/** Array: Destructor for each item in the array */
		STRUCT_DTOR			Destructor;
		/** True if object  has already been restored from data. False otherwise. */
		bool				bRestored = false;
		/** True if object has been finalized and generated diff data */
		bool				bFinalized = false;
		/** True if object has been snapshot before */
		bool				bSnapshot = false;
		/** True if record should serialize data as binary blob (more compact). False to use tagged serialization (more robust) */
		bool				bWantsBinarySerialization = true;
		/** The serialized object data */
		FSerializedObject	SerializedObject;
		/** The serialized object data that will be used when the transaction is flipped */
		FSerializedObject	SerializedObjectFlip;
		/** The serialized object data when it was last snapshot (if bSnapshot is true) */
		FSerializedObject	SerializedObjectSnapshot;
		/** Delta change information between the state of the object when the transaction started, and the state of the object when the transaction ended */
		FTransactionObjectDeltaChange DeltaChange;

		// Constructors.
		FObjectRecord() = default;
		FObjectRecord( FTransaction* Owner, UObject* InObject, TUniquePtr<FChange> InCustomChange, FScriptArray* InArray, int32 InIndex, int32 InCount, int32 InOper, int32 InElementSize, uint32 InElementAlignment, STRUCT_DC InDefaultConstructor, STRUCT_AR InSerializer, STRUCT_DTOR InDestructor );

	private:
		// Non-copyable
		FObjectRecord( const FObjectRecord& ) = delete;
		FObjectRecord& operator=( const FObjectRecord& ) = delete;

	public:
		// Functions.
		void SerializeContents( FArchive& Ar, int32 InOper );
		void SerializeObject( FArchive& Ar );
		void Restore( FTransaction* Owner );
		void Save( FTransaction* Owner );
		void Load( FTransaction* Owner );
		void Finalize( FTransaction* Owner, TSharedPtr<ITransactionObjectAnnotation>& OutFinalizedObjectAnnotation );
		void Snapshot( FTransaction* Owner, TArrayView<const FProperty*> Properties );
		static void Diff( const FTransaction* Owner, const FSerializedObject& OldSerializedObect, const FSerializedObject& NewSerializedObject, FTransactionObjectDeltaChange& OutDeltaChange, const bool bFullDiff = true );

		/** Used by GC to collect referenced objects. */
		void AddReferencedObjects( FReferenceCollector& Collector );

		/** @return True if this record contains a reference to a pie object */
		bool ContainsPieObject() const;

		/** @return true if the record has a delta change or a custom change */
		bool HasChanges() const;

		/** @return true if the record has a custom change and the change has expired */
		bool HasExpired() const;

		/** Transfers data from an array. */
		class FReader : public FArchiveUObject
		{
		public:
			FReader(
				FTransaction* InOwner,
				const FSerializedObject& InSerializedObject,
				bool bWantBinarySerialization
				)
				: Owner(InOwner)
				, SerializedObject(InSerializedObject)
				, Offset(0)
			{
				this->SetWantBinaryPropertySerialization(bWantBinarySerialization);
				this->SetIsLoading(true);
				this->SetIsTransacting(true);
			}

			virtual int64 Tell() override {return Offset;}
			virtual void Seek( int64 InPos ) override { Offset = InPos; }
			virtual int64 TotalSize() override { return SerializedObject.Data.Num(); }

		private:
			void Serialize( void* SerData, int64 Num ) override
			{
				if( Num )
				{
					checkSlow(Offset+Num<=SerializedObject.Data.Num());
					FMemory::Memcpy( SerData, &SerializedObject.Data[Offset], Num );
					Offset += Num;
				}
			}
			FArchive& operator<<( class FName& N ) override
			{
				int32 NameIndex = 0;
				(FArchive&)*this << NameIndex;
				N = SerializedObject.ReferencedNames[NameIndex];
				return *this;
			}
			FArchive& operator<<( class UObject*& Res ) override
			{
				int32 ObjectIndex = 0;
				(FArchive&)*this << ObjectIndex;
				if (ObjectIndex != INDEX_NONE)
				{
					Res = SerializedObject.ReferencedObjects[ObjectIndex].Get();
				}
				else
				{
					Res = nullptr;
				}
				return *this;
			}
			void Preload( UObject* InObject ) override
			{
				if (Owner)
				{
					if (const FObjectRecords* ObjectRecords = Owner->ObjectRecordsMap.Find(FPersistentObjectRef(InObject)))
					{
						for (FObjectRecord* Record : ObjectRecords->Records)
						{
							checkSlow(Record->Object.Get() == InObject);
							if (!Record->CustomChange)
							{
								Record->Restore(Owner);
							}
						}
					}
				}
			}
			FTransaction* Owner;
			const FSerializedObject& SerializedObject;
			int64 Offset;
		};

		/**
		 * Transfers data to an array.
		 */
		class FWriter : public FArchiveUObject
		{
		public:
			FWriter(
				FSerializedObject& InSerializedObject,
				bool bWantBinarySerialization,
				TArrayView<const FProperty*> InPropertiesToSerialize = TArrayView<const FProperty*>()
				)
				: SerializedObject(InSerializedObject)
				, PropertiesToSerialize(InPropertiesToSerialize)
				, Offset(0)
			{
				for(int32 ObjIndex = 0; ObjIndex < SerializedObject.ReferencedObjects.Num(); ++ObjIndex)
				{
					ObjectMap.Add(SerializedObject.ReferencedObjects[ObjIndex].Get(), ObjIndex);
				}

				for(int32 NameIndex = 0; NameIndex < SerializedObject.ReferencedNames.Num(); ++NameIndex)
				{
					NameMap.Add(SerializedObject.ReferencedNames[NameIndex], NameIndex);
				}

				this->SetWantBinaryPropertySerialization(bWantBinarySerialization);
				this->SetIsSaving(true);
				this->SetIsTransacting(true);
			}

			virtual int64 Tell() override {return Offset;}
			virtual void Seek( int64 InPos ) override
			{
				checkSlow(Offset<=SerializedObject.Data.Num());
				Offset = InPos; 
			}

			virtual int64 TotalSize() override { return SerializedObject.Data.Num(); }

			virtual bool ShouldSkipProperty(const FProperty* InProperty) const override
			{
				return (PropertiesToSerialize.Num() > 0 && !PropertiesToSerialize.Contains(InProperty))
					|| FArchiveUObject::ShouldSkipProperty(InProperty);
			}

		private:
			struct FCachedPropertyKey
			{
			public:
				FCachedPropertyKey()
					: LastUpdateCount(0)
				{
				}

				FName SyncCache(const FArchiveSerializedPropertyChain* InPropertyChain)
				{
					if (InPropertyChain)
					{
						const uint32 CurrentUpdateCount = InPropertyChain->GetUpdateCount();
						if (CurrentUpdateCount != LastUpdateCount)
						{
							CachedKey = InPropertyChain->GetNumProperties() > 0 ? FSerializedProperty::BuildSerializedPropertyKey(*InPropertyChain) : FName();
							LastUpdateCount = CurrentUpdateCount;
						}
					}
					else
					{
						CachedKey = FName();
						LastUpdateCount = 0;
					}

					return CachedKey;
				}

			private:
				FName CachedKey;
				uint32 LastUpdateCount;
			};

			void Serialize( void* SerData, int64 Num ) override
			{
				if( Num )
				{
					int64 DataIndex = ( Offset == SerializedObject.Data.Num() )
						?  SerializedObject.Data.AddUninitialized(Num)
						:  Offset;
					
					FMemory::Memcpy( &SerializedObject.Data[DataIndex], SerData, Num );
					Offset+= Num;

					// Track this property offset in the serialized data
					const FArchiveSerializedPropertyChain* PropertyChain = GetSerializedPropertyChain();
					if (PropertyChain && PropertyChain->GetNumProperties() > 0)
					{
						const FName SerializedTaggedPropertyKey = CachedSerializedTaggedPropertyKey.SyncCache(PropertyChain);
						FSerializedProperty& SerializedTaggedProperty = SerializedObject.SerializedProperties.FindOrAdd(SerializedTaggedPropertyKey);
						SerializedTaggedProperty.AppendSerializedData(DataIndex, Num);
					}
				}
			}
			FArchive& operator<<( class FName& N ) override
			{
				int32 NameIndex = INDEX_NONE;
				const int32 * NameIndexPtr = NameMap.Find(N);
				if (NameIndexPtr)
				{
					NameIndex = *NameIndexPtr;
				}
				else
				{
					NameIndex = SerializedObject.ReferencedNames.Add(N);
					NameMap.Add(N, NameIndex);
				}

				// Track this name index in the serialized data
				{
					const FArchiveSerializedPropertyChain* PropertyChain = GetSerializedPropertyChain();
					const FName SerializedTaggedPropertyKey = CachedSerializedTaggedPropertyKey.SyncCache(PropertyChain);
					SerializedObject.SerializedNameIndices.Add(SerializedTaggedPropertyKey, NameIndex);
				}

				return (FArchive&)*this << NameIndex;
			} 
			FArchive& operator<<( class UObject*& Res ) override
			{
				int32 ObjectIndex = INDEX_NONE;
				const int32* ObjIndexPtr = ObjectMap.Find(Res);
				if(ObjIndexPtr)
				{
					ObjectIndex = *ObjIndexPtr;
				}
				else if(Res)
				{
					ObjectIndex = SerializedObject.ReferencedObjects.Add(FPersistentObjectRef(Res));
					ObjectMap.Add(Res, ObjectIndex);
				}

				// Track this object offset in the serialized data
				{
					const FArchiveSerializedPropertyChain* PropertyChain = GetSerializedPropertyChain();
					const FName SerializedTaggedPropertyKey = CachedSerializedTaggedPropertyKey.SyncCache(PropertyChain);
					SerializedObject.SerializedObjectIndices.Add(SerializedTaggedPropertyKey, ObjectIndex);
				}

				return (FArchive&)*this << ObjectIndex;
			}
			FSerializedObject& SerializedObject;
			TArrayView<const FProperty*> PropertiesToSerialize;
			TMap<UObject*, int32> ObjectMap;
			TMap<FName, int32> NameMap;
			FCachedPropertyKey CachedSerializedTaggedPropertyKey;
			int64 Offset;
		};
	};

	struct FObjectRecords
	{
		friend FArchive& operator<<(FArchive& Ar, FObjectRecords& ObjectRecords)
		{
			Ar << ObjectRecords.SaveCount;

			if (Ar.IsLoading())
			{
				// Clear the map on load, as it will need to be rebuilt from the source array
				ObjectRecords.Records.Empty();
			}

			return Ar;
		}

		TArray<FObjectRecord*, TInlineAllocator<1>> Records;
		int32 SaveCount = 0;
	};

	// Transaction variables.
	/** List of object records in this transaction */
	TIndirectArray<FObjectRecord> Records;

	/** Map of object records (non-array), for optimized look-up and to prevent an object being serialized to a transaction more than once. */
	TMap<FPersistentObjectRef, FObjectRecords> ObjectRecordsMap;

	/** Unique identifier for this transaction, used to track it during its lifetime */
	FGuid		Id;

	/**
	 * Unique identifier for the active operation on this transaction (if any).
	 * This is set by a call to BeginOperation and cleared by a call to EndOperation.
	 * BeginOperation should be called when a transaction or undo/redo starts, and EndOperation should be called when a transaction is finalized or canceled or undo/redo ends.
	 */
	FGuid		OperationId;

	/** Description of the transaction. Can be used by UI */
	FText		Title;
	
	/** A text string describing the context for the transaction. Typically the name of the system causing the transaction*/
	FString		Context;

	/** The key object being edited in this transaction. For example the blueprint object. Can be NULL */
	UObject* PrimaryObject = nullptr;

	/** If true, on apply flip the direction of iteration over object records. The only client for which this is false is FMatineeTransaction */
	bool		bFlip = true;

	/** Used to track direction to iterate over transaction's object records. Typically -1 for Undo, 1 for Redo */
	int32		Inc = -1;

	struct FChangedObjectValue
	{
		FChangedObjectValue()
			: Annotation()
			, RecordIndex(INDEX_NONE)
		{
		}

		FChangedObjectValue(const int32 InRecordIndex, const TSharedPtr<ITransactionObjectAnnotation>& InAnnotation)
			: Annotation(InAnnotation)
			, RecordIndex(InRecordIndex)
		{
		}

		TSharedPtr<ITransactionObjectAnnotation> Annotation;
		int32 RecordIndex;
	};

	/** Objects that will be changed directly by the transaction, empty when not transacting */
	TMap<UObject*, FChangedObjectValue> ChangedObjects;

public:
	// Constructor.
	FTransaction(  const TCHAR* InContext=nullptr, const FText& InTitle=FText(), bool bInFlip=false )
		:	Id( FGuid::NewGuid() )
		,	Title( InTitle )
		,	Context( InContext )
		,	bFlip(bInFlip)
	{
	}

	virtual ~FTransaction()
	{
	}

private:
	// Non-copyable
	FTransaction( const FTransaction& ) = delete;
	FTransaction& operator=( const FTransaction& ) = delete;

public:
	
	// FTransactionBase interface.
	virtual void SaveObject( UObject* Object ) override;
	virtual void SaveArray( UObject* Object, FScriptArray* Array, int32 Index, int32 Count, int32 Oper, int32 ElementSize, uint32 ElementAlignment, STRUCT_DC DefaultConstructor, STRUCT_AR Serializer, STRUCT_DTOR Destructor ) override;
	virtual void StoreUndo( UObject* Object, TUniquePtr<FChange> UndoChange ) override;
	virtual void SetPrimaryObject(UObject* InObject) override;
	virtual void SnapshotObject( UObject* InObject, TArrayView<const FProperty*> Properties ) override;

	/** BeginOperation should be called when a transaction or undo/redo starts */
	virtual void BeginOperation() override;

	/** EndOperation should be called when a transaction is finalized or canceled or undo/redo ends */
	virtual void EndOperation() override;

	/**
	 * Enacts the transaction.
	 */
	virtual void Apply() override;

	/**
	 * Finalize the transaction (try and work out what's changed).
	 */
	virtual void Finalize() override;

	/**
	 * Gets the full context for the transaction.
	 */
	virtual FTransactionContext GetContext() const override
	{
		return FTransactionContext(Id, OperationId, Title, *Context, PrimaryObject);
	}

	/** @return true if the transaction is transient. */
	virtual bool IsTransient() const override;

	/** @return True if this record contains a reference to a pie object */
	virtual bool ContainsPieObjects() const override;

	/** @return true if the transaction contains custom changes and all the changes have expired */
	virtual bool HasExpired() const;


	/** Returns a unique string to serve as a type ID for the FTranscationBase-derived type. */
	virtual const TCHAR* GetTransactionType() const
	{
		return TEXT("FTransaction");
	}

	// FTransaction interface.
	SIZE_T DataSize() const;

	/** Returns the unique identifier for this transaction, used to track it during its lifetime */
	FGuid GetId() const
	{
		return Id;
	}

	/** Returns the unique identifier for the active operation on this transaction (if any) */
	FGuid GetOperationId() const
	{
		return OperationId;
	}

	/** Returns the descriptive text for the transaction */
	FText GetTitle() const
	{
		return Title;
	}

	/** Returns the description of each contained Object Record */
	FText GetDescription() const
	{			
		FString ConcatenatedRecords;
		for (const FObjectRecord& Record : Records)
		{
			if (Record.CustomChange.IsValid())
			{
				if (ConcatenatedRecords.Len())
				{
					ConcatenatedRecords += TEXT("\n");
				}
				ConcatenatedRecords += Record.CustomChange->ToString();
			}
		}

		return ConcatenatedRecords.IsEmpty() 
			? GetTitle() 
			: FText::FromString(ConcatenatedRecords);
	}

	/** Serializes a reference to a transaction in a given archive. */
	friend FArchive& operator<<( FArchive& Ar, FTransaction& T )
	{
		Ar << T.Records << T.ObjectRecordsMap << T.Id << T.Title << T.Context << T.PrimaryObject;

		if (Ar.IsLoading())
		{
			// Rebuild the LUT from the records array
			for (FObjectRecord& Record : T.Records)
			{
				if (!Record.Array)
				{
					// We should have already added a map entry for this object when loading T.ObjectRecordsMap, so check that we have (otherwise something is out-of-sync)
					FObjectRecords& ObjectRecords = T.ObjectRecordsMap.FindChecked(Record.Object);
					// Note: This is only safe to do because T.Records is a TIndirectArray
					ObjectRecords.Records.Add(&Record);
				}
			}
		}

		return Ar;
	}

	/** Serializes a reference to a transaction in a given archive. */
	friend FArchive& operator<<(FArchive& Ar, TSharedRef<FTransaction>& SharedT)
	{
		return Ar << SharedT.Get();
	}

	/** Used by GC to collect referenced objects. */
	void AddReferencedObjects( FReferenceCollector& Collector );

	/**
	 * Get all the objects that are part of this transaction.
	 * @param	Objects		[out] Receives the object list.  Previous contents are cleared.
	 */
	void GetTransactionObjects(TArray<UObject*>& Objects) const;
	void RemoveRecords( int32 Count = 1 );
	int32 GetRecordCount() const;

	const UObject* GetPrimaryObject() const { return PrimaryObject; }

	/** Checks if a specific object is in the transaction currently underway */
	bool IsObjectTransacting(const UObject* Object) const;

	/**
	 * Outputs the contents of the ObjectMap to the specified output device.
	 */
	void DumpObjectMap(FOutputDevice& Ar) const;

	/**
	 * Create a map that holds information about objects of a given transaction.
	 */
	FTransactionDiff GenerateDiff() const;

	// Transaction friends.
	friend FArchive& operator<<( FArchive& Ar, FTransaction::FObjectRecord& R );

	friend class FObjectRecord;
	friend class FObjectRecord::FReader;
	friend class FObjectRecord::FWriter;
};

UCLASS(abstract, transient)
class UNREALED_API UTransactor : public UObject
{
    GENERATED_UCLASS_BODY()
	/**
	 * Begins a new undo transaction.  An undo transaction is defined as all actions
	 * which take place when the user selects "undo" a single time.
	 * If there is already an active transaction in progress, increments that transaction's
	 * action counter instead of beginning a new transaction.
	 * 
	 * @param	SessionContext	the context for the undo session; typically the tool/editor that cause the undo operation
	 * @param	Description		the description for the undo session;  this is the text that 
	 *							will appear in the "Edit" menu next to the Undo item
	 *
	 * @return	Number of active actions when Begin() was called;  values greater than
	 *			0 indicate that there was already an existing undo transaction in progress.
	 */
	virtual int32 Begin( const TCHAR* SessionContext, const FText& Description ) PURE_VIRTUAL(UTransactor::Begin,return 0;);

	/**
	 * Attempts to close an undo transaction.  Only successful if the transaction's action
	 * counter is 1.
	 * 
	 * @return	Number of active actions when End() was called; a value of 1 indicates that the
	 *			transaction was successfully closed
	 */
	virtual int32 End() PURE_VIRTUAL(UTransactor::End,return 0;);

	/**
	 * Cancels the current transaction, no longer capture actions to be placed in the undo buffer.
	 *
	 * @param	StartIndex	the value of ActiveIndex when the transaction to be canceled was began. 
	 */
	virtual void Cancel( int32 StartIndex = 0 ) PURE_VIRTUAL(UTransactor::Cancel,);

	/**
	 * Resets the entire undo buffer;  deletes all undo transactions.
	 */
	virtual void Reset( const FText& Reason ) PURE_VIRTUAL(UTransactor::Reset,);

	/**
	 * Returns whether there are any active actions; i.e. whether actions are currently
	 * being captured into the undo buffer.
	 */
	virtual bool IsActive() PURE_VIRTUAL(UTransactor::IsActive,return false;);

	/**
	 * Determines whether the undo option should be selectable.
	 * 
	 * @param	Str		[out] the reason that undo is disabled
	 *
	 * @return	true if the "Undo" option should be selectable.
	 */
	virtual bool CanUndo( FText* Text=nullptr ) PURE_VIRTUAL(UTransactor::CanUndo,return false;);

	/**
	 * Determines whether the redo option should be selectable.
	 * 
	 * @param	Str		[out] the reason that redo is disabled
	 *
	 * @return	true if the "Redo" option should be selectable.
	 */
	virtual bool CanRedo( FText* Text=nullptr ) PURE_VIRTUAL(UTransactor::CanRedo,return false;);

	/**
	 * Gets the current length of the transaction queue.
	 *
	 * @return Queue length.
	 */
	virtual int32 GetQueueLength( ) const PURE_VIRTUAL(UTransactor::GetQueueLength,return 0;);

	/**
	 * Gets the transaction queue index from its TransactionId.
	 *
	 * @param TransactionId - The id of transaction in the queue.
	 *
	 * @return The queue index or INDEX_NONE if not found.
	 */
	virtual int32 FindTransactionIndex(const FGuid& TransactionId ) const PURE_VIRTUAL(UTransactor::FindTransactionIndex, return INDEX_NONE;);

	/**
	 * Gets the transaction at the specified queue index.
	 *
	 * @param QueueIndex - The index of the transaction in the queue.
	 *
	 * @return A read-only pointer to the transaction, or NULL if it does not exist.
	 */
	virtual const FTransaction* GetTransaction( int32 QueueIndex ) const PURE_VIRTUAL(UTransactor::GetTransaction,return nullptr;);

	/**
	 * Returns the description of the undo action that will be performed next.
	 * This is the text that is shown next to the "Undo" item in the menu.
	 * 
	 * @param	bCheckWhetherUndoPossible	Perform test whether undo is possible and return Error if not and option is set
	 *
	 * @return	text describing the next undo transaction
	 */
	virtual FTransactionContext GetUndoContext( bool bCheckWhetherUndoPossible = true ) PURE_VIRTUAL(UTransactor::GetUndoContext,return FTransactionContext(););

	/**
	 * Determines the amount of data currently stored by the transaction buffer.
	 *
	 * @return	number of bytes stored in the undo buffer
	 */
	virtual SIZE_T GetUndoSize() const PURE_VIRTUAL(UTransactor::GetUndoSize,return 0;);

	/**
	 * Gets the number of transactions that were undone and can be redone.
	 *
	 * @return Undo count.
	 */
	virtual int32 GetUndoCount( ) const PURE_VIRTUAL(UTransactor::GetUndoCount,return 0;);

	/**
	 * Returns the description of the redo action that will be performed next.
	 * This is the text that is shown next to the "Redo" item in the menu.
	 * 
	 * @return	text describing the next redo transaction
	 */
	virtual FTransactionContext GetRedoContext() PURE_VIRTUAL(UTransactor::GetRedoContext,return FTransactionContext(););

	/**
	 * Sets an undo barrier at the current point in the transaction buffer.
	 * Undoing beyond this point will not be allowed until the barrier is removed.
	 */
	virtual void SetUndoBarrier() PURE_VIRTUAL(UTransactor::SetUndoBarrier, );

	/**
	 * Removes the last set undo barrier from the transaction buffer.
	 */
	virtual void RemoveUndoBarrier() PURE_VIRTUAL(UTransactor::RemoveUndoBarrier, );

	/**
	 * Clears all undo barriers.
	 */
	virtual void ClearUndoBarriers() PURE_VIRTUAL(UTransactor::ClearUndoBarriers, );

	/**
	 * Executes an undo transaction, undoing all actions contained by that transaction.
	 * 
	 * @param	bCanRedo	If false indicates that the undone transaction (and any transactions that came after it) cannot be redone
	 * 
	 * @return				true if the transaction was successfully undone
	 */
	virtual bool Undo(bool bCanRedo = true) PURE_VIRTUAL(UTransactor::Undo,return false;);

	/**
	 * Executes an redo transaction, redoing all actions contained by that transaction.
	 * 
	 * @return				true if the transaction was successfully redone
	 */
	virtual bool Redo() PURE_VIRTUAL(UTransactor::Redo,return false;);

	/**
	 * Enables the transaction buffer to serialize the set of objects it references.
	 *
	 * @return	true if the transaction buffer is able to serialize object references.
	 */
	virtual bool EnableObjectSerialization() { return false; }

	/**
	 * Disables the transaction buffer from serializing the set of objects it references.
	 *
	 * @return	true if the transaction buffer is able to serialize object references.
	 */
	virtual bool DisableObjectSerialization() { return false; }

	/**
	 * Wrapper for checking if the transaction buffer is allowed to serialize object references.
	 */
	virtual bool IsObjectSerializationEnabled() { return false; }

	/** 
	 * Set passed object as the primary context object for transactions
	 */
	virtual void SetPrimaryUndoObject( UObject* Object ) PURE_VIRTUAL(UTransactor::SetPrimaryUndoObject,);

	/** Checks if a specific object is referenced by the transaction buffer */
	virtual bool IsObjectInTransationBuffer( const UObject* Object ) const { return false; }

	/** Checks if a specific object is in the transaction currently underway */
	virtual bool IsObjectTransacting(const UObject* Object) const PURE_VIRTUAL(UTransactor::IsObjectTransacting, return false;);

	/** @return True if this record contains a reference to a pie object */
	virtual bool ContainsPieObjects() const { return false; }
};
