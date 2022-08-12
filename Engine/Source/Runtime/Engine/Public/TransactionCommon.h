// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectPtr.h"
#include "UObject/WeakObjectPtr.h"
#include "Serialization/ArchiveUObject.h"
#include "Serialization/ArchiveSerializedPropertyChain.h"
#include "TransactionCommon.generated.h"

class FReferenceCollector;
struct FTransactionObjectDeltaChange;

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
USTRUCT()
struct ENGINE_API FTransactionPersistentObjectRef
{
	GENERATED_BODY()

public:
	FTransactionPersistentObjectRef() = default;
	explicit FTransactionPersistentObjectRef(UObject* InObject);

	bool Serialize(FArchive& Ar)
	{
		Ar << *this;
		return true;
	}

	friend FArchive& operator<<(FArchive& Ar, FTransactionPersistentObjectRef& ReferencedObject)
	{
		Ar << (std::underlying_type_t<EReferenceType>&)ReferencedObject.ReferenceType;
		Ar << ReferencedObject.RootObject;
		Ar << ReferencedObject.SubObjectHierarchyIDs;
		return Ar;
	}

	friend bool operator==(const FTransactionPersistentObjectRef& LHS, const FTransactionPersistentObjectRef& RHS)
	{
		return LHS.ReferenceType == RHS.ReferenceType
			&& LHS.RootObject == RHS.RootObject
			&& (LHS.ReferenceType != EReferenceType::SubObject || LHS.SubObjectHierarchyIDs == RHS.SubObjectHierarchyIDs);
	}

	friend bool operator!=(const FTransactionPersistentObjectRef& LHS, const FTransactionPersistentObjectRef& RHS)
	{
		return !(LHS == RHS);
	}

	friend uint32 GetTypeHash(const FTransactionPersistentObjectRef& InObjRef)
	{
		return HashCombine(GetTypeHash(InObjRef.ReferenceType), GetTypeHash(InObjRef.RootObject));
	}

	bool IsRootObjectReference() const
	{
		return ReferenceType == EReferenceType::RootObject;
	}

	bool IsSubObjectReference() const
	{
		return ReferenceType == EReferenceType::SubObject;
	}

	UObject* Get() const;
	
	void AddStructReferencedObjects(FReferenceCollector& Collector);

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

template<>
struct TStructOpsTypeTraits<FTransactionPersistentObjectRef> : public TStructOpsTypeTraitsBase2<FTransactionPersistentObjectRef>
{
	enum
	{
		WithIdenticalViaEquality = true,
		WithAddStructReferencedObjects = true,
		WithSerializer = true,
	};
};

USTRUCT()
struct ENGINE_API FTransactionSerializedTaggedData
{
	GENERATED_BODY()

public:
	static FTransactionSerializedTaggedData FromOffsetAndSize(const int64 InOffset, const int64 InSize);
	static FTransactionSerializedTaggedData FromStartAndEnd(const int64 InStart, const int64 InEnd);

	void AppendSerializedData(const int64 InOffset, const int64 InSize);
	void AppendSerializedData(const FTransactionSerializedTaggedData& InData);

	bool HasSerializedData() const;

	int64 GetStart() const
	{
		return DataOffset;
	}

	int64 GetEnd() const
	{
		return DataOffset + DataSize;
	}

	friend bool operator==(const FTransactionSerializedTaggedData& LHS, const FTransactionSerializedTaggedData& RHS)
	{
		return LHS.DataOffset == RHS.DataOffset
			&& LHS.DataSize == RHS.DataSize;
	}

	friend bool operator!=(const FTransactionSerializedTaggedData& LHS, const FTransactionSerializedTaggedData& RHS)
	{
		return !(LHS == RHS);
	}

	/** Offset to the start of the tagged data within the serialized object */
	UPROPERTY()
	int64 DataOffset = INDEX_NONE;

	/** Size (in bytes) of the tagged data within the serialized object */
	UPROPERTY()
	int64 DataSize = 0;
};

USTRUCT()
struct ENGINE_API FTransactionSerializedObjectData
{
	GENERATED_BODY()

public:
	bool Serialize(FArchive& Ar)
	{
		Ar << *this;
		return true;
	}

	friend FArchive& operator<<(FArchive& Ar, FTransactionSerializedObjectData& SerializedData)
	{
		Ar << SerializedData.Data;
		return Ar;
	}

	friend bool operator==(const FTransactionSerializedObjectData& LHS, const FTransactionSerializedObjectData& RHS)
	{
		return LHS.Data == RHS.Data;
	}

	friend bool operator!=(const FTransactionSerializedObjectData& LHS, const FTransactionSerializedObjectData& RHS)
	{
		return !(LHS == RHS);
	}

	void Read(void* Dest, int64 Offset, int64 Num) const;
	void Write(const void* Src, int64 Offset, int64 Num);
	
	const void* GetPtr(int64 Offset) const
	{
		return &Data[Offset];
	}

	void Reset()
	{
		Data.Reset();
	}

	int64 Num() const
	{
		return Data.Num();
	}

private:
	TArray64<uint8> Data;
};

template<>
struct TStructOpsTypeTraits<FTransactionSerializedObjectData> : public TStructOpsTypeTraitsBase2<FTransactionSerializedObjectData>
{
	enum
	{
		WithIdenticalViaEquality = true,
		WithSerializer = true,
	};
};

USTRUCT()
struct FTransactionSerializedIndices
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<int32> Indices;
};

USTRUCT()
struct FTransactionSerializedObjectInfo
{
	GENERATED_BODY()

public:
	void SetObject(const UObject* InObject)
	{
		ObjectPackageName = InObject->GetPackage()->GetFName();
		ObjectName = InObject->GetFName();
		ObjectPathName = *InObject->GetPathName();
		ObjectOuterPathName = InObject->GetOuter() ? FName(*InObject->GetOuter()->GetPathName()) : FName();
		ObjectExternalPackageName = InObject->GetExternalPackage() ? InObject->GetExternalPackage()->GetFName() : FName();
		ObjectClassPathName = FName(*InObject->GetClass()->GetPathName());
		bIsPendingKill = IsValid(InObject);
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
	}

	void Swap(FTransactionSerializedObjectInfo& Other)
	{
		Exchange(ObjectPackageName, Other.ObjectPackageName);
		Exchange(ObjectName, Other.ObjectName);
		Exchange(ObjectPathName, Other.ObjectPathName);
		Exchange(ObjectOuterPathName, Other.ObjectOuterPathName);
		Exchange(ObjectExternalPackageName, Other.ObjectExternalPackageName);
		Exchange(ObjectClassPathName, Other.ObjectClassPathName);
		Exchange(bIsPendingKill, Other.bIsPendingKill);
	}

	/** The package name of the object when it was serialized, can be dictated either by outer chain or external package */
	UPROPERTY()
	FName ObjectPackageName;

	/** The name of the object when it was serialized */
	UPROPERTY()
	FName ObjectName;

	/** The path name of the object when it was serialized */
	UPROPERTY()
	FName ObjectPathName;

	/** The outer path name of the object when it was serialized */
	UPROPERTY()
	FName ObjectOuterPathName;

	/** The external package name of the object when it was serialized, if any */
	UPROPERTY()
	FName ObjectExternalPackageName;

	/** The path name of the object's class. */
	UPROPERTY()
	FName ObjectClassPathName;

	/** The pending kill state of the object when it was serialized */
	UPROPERTY()
	bool bIsPendingKill = false;
};

USTRUCT()
struct FTransactionSerializedObject
{
	GENERATED_BODY()

public:
	void Reset()
	{
		SerializedData.Reset();
		ReferencedObjects.Reset();
		ReferencedNames.Reset();
	}

	void Swap(FTransactionSerializedObject& Other)
	{
		Exchange(SerializedData, Other.SerializedData);
		Exchange(ReferencedObjects, Other.ReferencedObjects);
		Exchange(ReferencedNames, Other.ReferencedNames);
	}

	/** The serialized data for the transacted object */
	UPROPERTY()
	FTransactionSerializedObjectData SerializedData;

	/** External objects referenced by the transacted object */
	UPROPERTY()
	TArray<FTransactionPersistentObjectRef> ReferencedObjects;
	
	/** Names referenced by the transacted object */
	UPROPERTY()
	TArray<FName> ReferencedNames;
};

USTRUCT()
struct FTransactionDiffableObject
{
	GENERATED_BODY()

public:
	void SetObject(const UObject* InObject)
	{
		ObjectInfo.SetObject(InObject);
	}

	void Reset()
	{
		ObjectInfo.Reset();
		SerializedData.Reset();
		SerializedTaggedData.Reset();
	}

	void Swap(FTransactionDiffableObject& Other)
	{
		ObjectInfo.Swap(Other.ObjectInfo);
		Exchange(SerializedData, Other.SerializedData);
		Exchange(SerializedTaggedData, Other.SerializedTaggedData);
	}

	/** Information about the object when it was serialized */
	UPROPERTY()
	FTransactionSerializedObjectInfo ObjectInfo;

	/** The serialized data for the diffable object */
	UPROPERTY()
	FTransactionSerializedObjectData SerializedData;

	/** Information about tagged data (mainly properties) that were serialized within this object */
	UPROPERTY()
	TMap<FName, FTransactionSerializedTaggedData> SerializedTaggedData;
};

namespace UE::Transaction
{

ENGINE_API extern const FName TaggedDataKey_UnknownData;
ENGINE_API extern const FName TaggedDataKey_ScriptData;

/** Core archive to read a transaction object from the buffer. */
class ENGINE_API FSerializedObjectDataReader : public FArchiveUObject
{
public:
	FSerializedObjectDataReader(const FTransactionSerializedObject& InSerializedObject);

	virtual int64 Tell() override { return Offset; }
	virtual void Seek(int64 InPos) override { Offset = InPos; }
	virtual int64 TotalSize() override { return SerializedObject.SerializedData.Num(); }

protected:
	virtual void Serialize(void* SerData, int64 Num) override;

	using FArchiveUObject::operator<<;
	virtual FArchive& operator<<(class FName& N) override;
	virtual FArchive& operator<<(class UObject*& Res) override;

	const FTransactionSerializedObject& SerializedObject;
	int64 Offset = 0;
};

namespace Internal
{

/** Core archive to write a transaction object to the buffer. */
class ENGINE_API FSerializedObjectDataWriterCommon : public FArchiveUObject
{
public:
	FSerializedObjectDataWriterCommon(FTransactionSerializedObjectData& InSerializedData);

	virtual int64 Tell() override { return Offset; }
	virtual void Seek(int64 InPos) override { checkSlow(Offset <= SerializedData.Num()); Offset = InPos; }
	virtual int64 TotalSize() override { return SerializedData.Num(); }

protected:
	virtual void Serialize(void* SerData, int64 Num) override;

	virtual void OnDataSerialized(int64 InOffset, int64 InNum) {}

	FTransactionSerializedObjectData& SerializedData;
	int64 Offset = 0;
};

} // namespace Internal

/** Core archive to write a transaction object to the buffer. */
class ENGINE_API FSerializedObjectDataWriter : public Internal::FSerializedObjectDataWriterCommon
{
public:
	FSerializedObjectDataWriter(FTransactionSerializedObject& InSerializedObject);

protected:
	using FArchiveUObject::operator<<;
	virtual FArchive& operator<<(class FName& N) override;
	virtual FArchive& operator<<(class UObject*& Res) override;

	FTransactionSerializedObject& SerializedObject;
	int64 Offset = 0;

	TMap<UObject*, int32> ObjectMap;
	TMap<FName, int32> NameMap;
};

/** Core archive to write a diffable object to the buffer. */
class ENGINE_API FDiffableObjectDataWriter : public Internal::FSerializedObjectDataWriterCommon
{
public:
	FDiffableObjectDataWriter(FTransactionDiffableObject& InDiffableObject, TArrayView<const FProperty*> InPropertiesToSerialize = TArrayView<const FProperty*>());

protected:
	FName GetTaggedDataKey() const;

	bool DoesObjectMatchDiffableObject(const UObject* Obj) const;

	virtual bool ShouldSkipProperty(const FProperty* InProperty) const override;

	virtual void MarkScriptSerializationStart(const UObject* Obj) override;
	virtual void MarkScriptSerializationEnd(const UObject* Obj) override;

	virtual void OnDataSerialized(int64 InOffset, int64 InNum) override;

	using FArchiveUObject::operator<<;
	virtual FArchive& operator<<(class FName& N) override;
	virtual FArchive& operator<<(class UObject*& Res) override;

private:
	struct FCachedPropertyKey
	{
	public:
		FName SyncCache(const FArchiveSerializedPropertyChain* InPropertyChain);

	private:
		FName CachedKey;
		uint32 LastUpdateCount = 0;
	};

	FTransactionDiffableObject& DiffableObject;
	TArrayView<const FProperty*> PropertiesToSerialize;

	bool bIsPerformingScriptSerialization = false;

	mutable bool bWasUsingTaggedDataKey_UnknownData = false;
	mutable bool bWasUsingTaggedDataKey_ScriptData = false;
	mutable int32 TaggedDataKeyIndex_UnknownData = 0;
	mutable int32 TaggedDataKeyIndex_ScriptData = 0;
	mutable FCachedPropertyKey CachedSerializedTaggedPropertyKey;
};

namespace DiffUtil
{

ENGINE_API void GenerateObjectDiff(const FTransactionDiffableObject& OldDiffableObject, const FTransactionDiffableObject& NewDiffableObject, FTransactionObjectDeltaChange& OutDeltaChange, const bool bFullDiff = true);

} // namespace DiffUtil

} // namespace UE::Transaction
