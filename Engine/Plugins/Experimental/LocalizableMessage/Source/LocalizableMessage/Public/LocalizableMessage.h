// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "LocalizableMessageParameter.h"
#include "Templates/UniquePtr.h"

#include "LocalizableMessage.generated.h"

USTRUCT()
struct FLocalizableMessageParameterEntry
{
	GENERATED_BODY()

public:

	FLocalizableMessageParameterEntry();
	LOCALIZABLEMESSAGE_API FLocalizableMessageParameterEntry(const FString& InKey, TUniquePtr<FLocalizableMessageParameter, FLocalizableMessageParameter::FCustomDeleter>&& InValue);
	FLocalizableMessageParameterEntry(FLocalizableMessageParameterEntry&&);
	~FLocalizableMessageParameterEntry();
	FLocalizableMessageParameterEntry& operator=(FLocalizableMessageParameterEntry&&);

	bool operator==(const FLocalizableMessageParameterEntry& Other) const;

	void AddStructReferencedObjects(class FReferenceCollector& Collector);
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);

	UPROPERTY()
	FString Key;

	TUniquePtr<FLocalizableMessageParameter, FLocalizableMessageParameter::FCustomDeleter> Value;
};

template<>
struct TStructOpsTypeTraits< FLocalizableMessageParameterEntry > : public TStructOpsTypeTraitsBase2< FLocalizableMessageParameterEntry >
{
	enum
	{
		WithAddStructReferencedObjects = true,
		WithNetSerializer = true,
		WithCopy = false,
	};
};


USTRUCT()
struct FLocalizableMessage
{
	GENERATED_BODY();

	LOCALIZABLEMESSAGE_API FLocalizableMessage();
	LOCALIZABLEMESSAGE_API FLocalizableMessage(FLocalizableMessage&&);
	LOCALIZABLEMESSAGE_API ~FLocalizableMessage();
	LOCALIZABLEMESSAGE_API FLocalizableMessage& operator=(FLocalizableMessage&&);

	LOCALIZABLEMESSAGE_API bool operator==(const FLocalizableMessage& Other) const;
	LOCALIZABLEMESSAGE_API bool operator!=(const FLocalizableMessage& Other) const { return !(*this == Other); }

	UPROPERTY()
	FString Key;

	UPROPERTY()
	FString DefaultText;

	UPROPERTY()
	TArray<FLocalizableMessageParameterEntry> Substitutions;
};

template<>
struct TStructOpsTypeTraits< FLocalizableMessage > : public TStructOpsTypeTraitsBase2< FLocalizableMessage >
{
	enum
	{
		WithCopy = false,
	};
};