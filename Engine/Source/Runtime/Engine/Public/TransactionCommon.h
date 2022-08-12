// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectPtr.h"
#include "UObject/WeakObjectPtr.h"
#include "Serialization/ArchiveUObject.h"
#include "Serialization/ArchiveSerializedPropertyChain.h"

class FReferenceCollector;
struct FTransactionObjectDeltaChange;

namespace UE::Transaction
{

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
struct ENGINE_API FPersistentObjectRef
{
public:
	FPersistentObjectRef() = default;
	explicit FPersistentObjectRef(UObject* InObject);

	bool Serialize(FArchive& Ar)
	{
		Ar << *this;
		return true;
	}

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

struct ENGINE_API FSerializedTaggedData
{
public:
	static FSerializedTaggedData FromOffsetAndSize(const int64 InOffset, const int64 InSize);
	static FSerializedTaggedData FromStartAndEnd(const int64 InStart, const int64 InEnd);

	void AppendSerializedData(const int64 InOffset, const int64 InSize);
	void AppendSerializedData(const FSerializedTaggedData& InData);

	bool HasSerializedData() const;

	int64 GetStart() const
	{
		return DataOffset;
	}

	int64 GetEnd() const
	{
		return DataOffset + DataSize;
	}

	friend bool operator==(const FSerializedTaggedData& LHS, const FSerializedTaggedData& RHS)
	{
		return LHS.DataOffset == RHS.DataOffset
			&& LHS.DataSize == RHS.DataSize;
	}

	friend bool operator!=(const FSerializedTaggedData& LHS, const FSerializedTaggedData& RHS)
	{
		return !(LHS == RHS);
	}

	/** Offset to the start of the tagged data within the serialized object */
	int64 DataOffset = INDEX_NONE;

	/** Size (in bytes) of the tagged data within the serialized object */
	int64 DataSize = 0;
};

struct ENGINE_API FSerializedObjectData
{
public:
	bool Serialize(FArchive& Ar)
	{
		Ar << *this;
		return true;
	}

	friend FArchive& operator<<(FArchive& Ar, FSerializedObjectData& SerializedData)
	{
		Ar << SerializedData.Data;
		return Ar;
	}

	friend bool operator==(const FSerializedObjectData& LHS, const FSerializedObjectData& RHS)
	{
		return LHS.Data == RHS.Data;
	}

	friend bool operator!=(const FSerializedObjectData& LHS, const FSerializedObjectData& RHS)
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

struct FSerializedObjectInfo
{
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

	void Swap(FSerializedObjectInfo& Other)
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
	bool bIsPendingKill = false;
};

struct FSerializedObject
{
public:
	void Reset()
	{
		SerializedData.Reset();
		ReferencedObjects.Reset();
		ReferencedNames.Reset();
	}

	void Swap(FSerializedObject& Other)
	{
		Exchange(SerializedData, Other.SerializedData);
		Exchange(ReferencedObjects, Other.ReferencedObjects);
		Exchange(ReferencedNames, Other.ReferencedNames);
	}

	/** The serialized data for the transacted object */
	FSerializedObjectData SerializedData;

	/** External objects referenced by the transacted object */
	TArray<FPersistentObjectRef> ReferencedObjects;
	
	/** Names referenced by the transacted object */
	TArray<FName> ReferencedNames;
};

struct FDiffableObject
{
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

	void Swap(FDiffableObject& Other)
	{
		ObjectInfo.Swap(Other.ObjectInfo);
		Exchange(SerializedData, Other.SerializedData);
		Exchange(SerializedTaggedData, Other.SerializedTaggedData);
	}

	/** Information about the object when it was serialized */
	FSerializedObjectInfo ObjectInfo;

	/** The serialized data for the diffable object */
	FSerializedObjectData SerializedData;

	/** Information about tagged data (mainly properties) that were serialized within this object */
	TMap<FName, FSerializedTaggedData> SerializedTaggedData;
};

ENGINE_API extern const FName TaggedDataKey_UnknownData;
ENGINE_API extern const FName TaggedDataKey_ScriptData;

/** Core archive to read a transaction object from the buffer. */
class ENGINE_API FSerializedObjectDataReader : public FArchiveUObject
{
public:
	FSerializedObjectDataReader(const FSerializedObject& InSerializedObject);

	virtual int64 Tell() override { return Offset; }
	virtual void Seek(int64 InPos) override { Offset = InPos; }
	virtual int64 TotalSize() override { return SerializedObject.SerializedData.Num(); }

protected:
	virtual void Serialize(void* SerData, int64 Num) override;

	using FArchiveUObject::operator<<;
	virtual FArchive& operator<<(class FName& N) override;
	virtual FArchive& operator<<(class UObject*& Res) override;

	const FSerializedObject& SerializedObject;
	int64 Offset = 0;
};

namespace Internal
{

/** Core archive to write a transaction object to the buffer. */
class ENGINE_API FSerializedObjectDataWriterCommon : public FArchiveUObject
{
public:
	FSerializedObjectDataWriterCommon(FSerializedObjectData& InSerializedData);

	virtual int64 Tell() override { return Offset; }
	virtual void Seek(int64 InPos) override { checkSlow(Offset <= SerializedData.Num()); Offset = InPos; }
	virtual int64 TotalSize() override { return SerializedData.Num(); }

protected:
	virtual void Serialize(void* SerData, int64 Num) override;

	virtual void OnDataSerialized(int64 InOffset, int64 InNum) {}

	FSerializedObjectData& SerializedData;
	int64 Offset = 0;
};

} // namespace Internal

/** Core archive to write a transaction object to the buffer. */
class ENGINE_API FSerializedObjectDataWriter : public Internal::FSerializedObjectDataWriterCommon
{
public:
	FSerializedObjectDataWriter(FSerializedObject& InSerializedObject);

protected:
	using FArchiveUObject::operator<<;
	virtual FArchive& operator<<(class FName& N) override;
	virtual FArchive& operator<<(class UObject*& Res) override;

	FSerializedObject& SerializedObject;
	int64 Offset = 0;

	TMap<UObject*, int32> ObjectMap;
	TMap<FName, int32> NameMap;
};

/** Core archive to write a diffable object to the buffer. */
class ENGINE_API FDiffableObjectDataWriter : public Internal::FSerializedObjectDataWriterCommon
{
public:
	FDiffableObjectDataWriter(FDiffableObject& InDiffableObject, TArrayView<const FProperty*> InPropertiesToSerialize = TArrayView<const FProperty*>());

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

	FDiffableObject& DiffableObject;
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

enum EGetDiffableObjectMode : uint8
{
	SerializeObject,
	SerializeProperties,
};
ENGINE_API FDiffableObject GetDiffableObject(const UObject* Object, const EGetDiffableObjectMode Mode = EGetDiffableObjectMode::SerializeObject);

ENGINE_API FTransactionObjectDeltaChange GenerateObjectDiff(const FDiffableObject& OldDiffableObject, const FDiffableObject& NewDiffableObject, const bool bFullDiff = true);
ENGINE_API void GenerateObjectDiff(const FDiffableObject& OldDiffableObject, const FDiffableObject& NewDiffableObject, FTransactionObjectDeltaChange& OutDeltaChange, const bool bFullDiff = true);

} // namespace DiffUtil

} // namespace UE::Transaction
