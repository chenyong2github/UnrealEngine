// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "Animation/AnimBoneCompressionSettings.h"
#include "Animation/AnimCurveCompressionSettings.h"
#include "Animation/MirrorDataTable.h"
#include "Containers/Set.h"
#include "Containers/UnrealString.h"
#include "Factories/FbxAnimSequenceImportData.h"
#include "Hash/Blake3.h"
#include "UObject/DevObjectVersion.h"
#include "UObject/Object.h"
#include "UObject/UnrealType.h"
#include "Serialization/ArchiveUObject.h"

namespace UE::PoseSearch
{
// log properties and UObjects names
#ifndef UE_POSE_SEARCH_DERIVED_DATA_LOGGING
	#define UE_POSE_SEARCH_DERIVED_DATA_LOGGING 0
#endif

// log properties data
#ifndef UE_POSE_SEARCH_DERIVED_DATA_LOGGING_VERBOSE
	#define UE_POSE_SEARCH_DERIVED_DATA_LOGGING_VERBOSE 0
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

	FDerivedDataKeyBuilder(UObject* Object)
	{
		check(Object);

		ArIgnoreOuterRef = true;

		// Set FDerivedDataKeyBuilder to be a saving archive instead of a reference collector.
		// Reference collection causes FSoftObjectPtrs to be serialized by their weak pointer,
		// which doesn't give a stable hash.  Serializing these to a saving archive will
		// use a string reference instead, which is a more meaningful hash value.
		SetIsSaving(true);

		// used to invalidate the key without having to change POSESEARCHDB_DERIVEDDATA_VER all the times
		FGuid VersionGuid = FDevSystemGuids::GetSystemGuid(FDevSystemGuids::Get().POSESEARCHDB_DERIVEDDATA_VER);
		int32 POSESEARCHDB_DERIVEDDATA_VER_SMALL = 3;

		*this << VersionGuid;
		*this << POSESEARCHDB_DERIVEDDATA_VER_SMALL;
		*this << Object;
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

		if (!InProperty->HasAllPropertyFlags(CPF_Edit)) // bIsEditAnywhereProperty
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
		UE_LOG(LogPoseSearch, Log, TEXT("%s - %s"), *GetIndentation(), *InProperty->GetFullName());
		#endif

		return false;
	}

	virtual void Serialize(void* Data, int64 Length) override
	{
		const uint8* HasherData = reinterpret_cast<uint8*>(Data);

		#if UE_POSE_SEARCH_DERIVED_DATA_LOGGING_VERBOSE
		FString RawBytesString = BytesToString(HasherData, Length);
		UE_LOG(LogPoseSearch, Log, TEXT("%s  > %s"), *GetIndentation(), *RawBytesString);
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
		if (Object)
		{
			#if UE_POSE_SEARCH_DERIVED_DATA_LOGGING
			++Indentation;
			#endif

			bool bAlreadyProcessed = false;
			ObjectsAlreadySerialized.Add(Object, &bAlreadyProcessed);

			// If we haven't already serialized this object
			if (bAlreadyProcessed)
			{
				#if UE_POSE_SEARCH_DERIVED_DATA_LOGGING
				UE_LOG(LogPoseSearch, Log, TEXT("%sAlreadyProcessed '%s' (%s)"), *GetIndentation(), *Object->GetName(), *Object->GetClass()->GetName());
				#endif
			}
			// for specific types we only add their names to the hash
			else if (AddNameOnly(Object))
			{
				#if UE_POSE_SEARCH_DERIVED_DATA_LOGGING
				UE_LOG(LogPoseSearch, Log, TEXT("%sAddingNameOnly '%s' (%s)"), *GetIndentation(), *Object->GetName(), *Object->GetClass()->GetName());
				#endif

				FString ObjectName = GetFullNameSafe(Object);
				*this << ObjectName;
			}
			else
			{
				// Serialize it
				ObjectBeingSerialized = Object;

				#if UE_POSE_SEARCH_DERIVED_DATA_LOGGING
				UE_LOG(LogPoseSearch, Log, TEXT("%sBegin '%s' (%s)"), *GetIndentation(), *Object->GetName(), *Object->GetClass()->GetName());
				#endif

				const_cast<UObject*>(Object)->Serialize(*this);

				#if UE_POSE_SEARCH_DERIVED_DATA_LOGGING
				UE_LOG(LogPoseSearch, Log, TEXT("%sEnd '%s' (%s)"), *GetIndentation(), *Object->GetName(), *Object->GetClass()->GetName());
				#endif

				ObjectBeingSerialized = nullptr;
			}
			
			#if UE_POSE_SEARCH_DERIVED_DATA_LOGGING
			--Indentation;
			#endif
		}

		return *this;
	}

	virtual FString GetArchiveName() const override
	{
		return TEXT("FDerivedDataKeyBuilder");
	}
	//~ End FArchive Interface
	
	HashDigestType Finalize() const
	{
		return Hasher.Finalize();
	}

	const TSet<const UObject*>& GetDependencies() const
	{
		return ObjectsAlreadySerialized;
	}

protected:
	/** to keep the key generation lightweight, we hash only the full names for these types */
	bool AddNameOnly(class UObject* Object) const
	{
		return
			Cast<UAnimBoneCompressionSettings>(Object) ||
			Cast<UAnimCurveCompressionSettings>(Object) ||
			Cast<UFbxAnimSequenceImportData>(Object) ||
			Cast<UMirrorDataTable>(Object) ||
			Cast<USkeleton>(Object);
	}

#if UE_POSE_SEARCH_DERIVED_DATA_LOGGING
	FString GetIndentation() const
	{
		FString IndentationString;
		for (int32 i = 0; i < Indentation; ++i)
		{
			IndentationString.Append(" ");
		}
		return IndentationString;
	}

	int32 Indentation = 0;
#endif

	HashBuilderType Hasher;

	/** Set of objects that have already been serialized */
	TSet<const UObject*> ObjectsAlreadySerialized;

	/** Object currently being serialized */
	const UObject* ObjectBeingSerialized = nullptr;
};

} // namespace UE::PoseSearch

#endif // WITH_EDITOR