// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Queue.h"
#include "Containers/Set.h"
#include "Containers/UnrealString.h"
#include "Serialization/ArchiveUObject.h"
#include "Hash/Blake3.h"
#include "UObject/UnrealType.h"
#include "UObject/Object.h"

#if WITH_EDITORONLY_DATA

namespace UE::PoseSearch
{

#ifndef UE_POSE_SEARCH_DERIVED_DATA_LOGGING
	#define UE_POSE_SEARCH_DERIVED_DATA_LOGGING 0
#endif

//////////////////////////////////////////////////////////////////////////
// FDerivedDataKeyBuilder
class POSESEARCH_API FDerivedDataKeyBuilder : public FArchiveUObject
{
public:
	using Super = FArchiveUObject;
	using HashDigestType = FBlake3Hash;
	using HashBuilderType = FBlake3;

	inline static const FName ExcludeFromHashName = FName(TEXT("ExcludeFromHash"));
	inline static const FName NeverInHashName = FName(TEXT("NeverInHash"));

	FDerivedDataKeyBuilder()
	{
		ArIgnoreOuterRef = true;

		// Set FDerivedDataKeyBuilder to be a saving archive instead of a reference collector.
		// Reference collection causes FSoftObjectPtrs to be serialized by their weak pointer,
		// which doesn't give a stable hash.  Serializing these to a saving archive will
		// use a string reference instead, which is a more meaningful hash value.
		SetIsSaving(true);
	}

	virtual void Seek(int64 InPos) override
	{
		checkf(InPos == Tell(), TEXT("A hash cannot be computed when serialization relies on seeking."));
		FArchiveUObject::Seek(InPos);
	}

	//~ Begin FArchive Interface
	virtual bool ShouldSkipProperty(const FProperty* InProperty) const override
	{
		if (InProperty == nullptr)
		{
			return false;
		}

		if (Super::ShouldSkipProperty(InProperty))
		{
			return true;
		}

		if (InProperty->HasAllPropertyFlags(CPF_Transient))
		{
			return true;
		}
		
		if (InProperty->HasMetaData(ExcludeFromHashName))
		{
			return true;
		}
		
		check(!InProperty->HasMetaData(NeverInHashName));

#if UE_POSE_SEARCH_DERIVED_DATA_LOGGING
		UE_LOG(LogPoseSearch, Log, TEXT("    %s"), *InProperty->GetFullName());
#endif
		return false;
	}

	virtual void Serialize(void* Data, int64 Length) override
	{
		const uint8* HasherData = reinterpret_cast<uint8*>(Data);

#if UE_POSE_SEARCH_DERIVED_DATA_LOGGING
		FString RawBytesString = BytesToString(HasherData, Length);
		UE_LOG(LogPoseSearch, Log, TEXT("    %s"), *RawBytesString);
#endif
		Hasher.Update(HasherData, Length);
	}

	// @todo: do we need those Serialize(s)?
	//virtual void SerializeBits(void* Bits, int64 LengthBits) override
	//virtual void SerializeInt(uint32& Value, uint32 Max) override
	//virtual void SerializeIntPacked(uint32& Value) override

	using Super::IsSaving;
	using Super::operator<<; // For visibility of the overloads we don't override

	virtual FArchive& operator<<(FName& Name) override
	{
		// Don't include the name of the object being serialized, since that isn't technically part of the object's state
		if (!ObjectBeingSerialized || (Name != ObjectBeingSerialized->GetFName()))
		{
			// we cannot use GetTypeHash(Name) since it's bound to be non deterministic between editor restarts, so we convert the name into an FString and let the Serialize(void* Data, int64 Length) deal with it
			FString NameString = Name.ToString();
			*this << NameString;
		}
		return *this;
	}

	virtual FArchive& operator<<(class UObject*& Object) override
	{
		if (!RootObject || !Object || !Object->IsIn(RootObject))
		{
			auto UniqueName = GetPathNameSafe(Object);
			*this << UniqueName;
		}
		else
		{
			ObjectsToSerialize.Enqueue(Object);
		}
		return *this;
	}


	virtual FString GetArchiveName() const override
	{
		return TEXT("FDerivedDataKeyBuilder");
	}
	//~ End FArchive Interface

	/**
	* Serialize the given object and update the hash
	*/
	void Update(const UObject* Object, const UObject* Root)
	{
		RootObject = Root;
		if (Object)
		{
			// Start with the given object
			ObjectsToSerialize.Enqueue(Object);

			// Continue until we no longer have any objects to serialize
			while (ObjectsToSerialize.Dequeue(Object))
			{
				bool bAlreadyProcessed = false;
				ObjectsAlreadySerialized.Add(Object, &bAlreadyProcessed);
				// If we haven't already serialized this object
				if (!bAlreadyProcessed)
				{
					// Serialize it
					ObjectBeingSerialized = Object;
					const_cast<UObject*>(Object)->Serialize(*this);
					ObjectBeingSerialized = nullptr;
				}
			}

			// Cleanup
			RootObject = nullptr;
		}
	}

	void Update(const UObject* Object)
	{
		Update(Object, Object);
	}

	HashDigestType Finalize()
	{
		return Hasher.Finalize();
	}

protected:
	HashBuilderType Hasher;

	/** Queue of object references awaiting serialization */
	TQueue<const UObject*> ObjectsToSerialize;

	/** Set of objects that have already been serialized */
	TSet<const UObject*> ObjectsAlreadySerialized;

	/** Object currently being serialized */
	const UObject* ObjectBeingSerialized = nullptr;

	/** Root of object currently being serialized */
	const UObject* RootObject = nullptr;
};

} // namespace UE::PoseSearch

#endif // WITH_EDITORONLY_DATA