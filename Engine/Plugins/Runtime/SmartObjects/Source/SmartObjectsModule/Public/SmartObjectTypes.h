// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "SmartObjectTypes.generated.h"

#define WITH_SMARTOBJECT_DEBUG (!(UE_BUILD_SHIPPING || UE_BUILD_SHIPPING_WITH_EDITOR || UE_BUILD_TEST) && 1)

SMARTOBJECTSMODULE_API DECLARE_LOG_CATEGORY_EXTERN(LogSmartObject, Warning, All);

namespace UE { namespace SmartObject {

typedef uint32 ID;
static constexpr ID InvalidID = 0;

}} // UE::SmartObject

/**
 * Helper struct to wrap basic functionalities to store and use sequential SmartObject::ID.
 * Those IDs are only assigned at runtime.
 */
struct SMARTOBJECTSMODULE_API FSmartObjectSequentialID
{
	FSmartObjectSequentialID() : Value(UE::SmartObject::InvalidID)
	{}
	FSmartObjectSequentialID(const FSmartObjectSequentialID& Other) : Value(Other.Value)
	{}
	explicit FSmartObjectSequentialID(const UE::SmartObject::ID InID) : Value(InID)
	{}

	bool operator==(const FSmartObjectSequentialID& Other) const { return Value == Other.Value; }
	bool operator!=(const FSmartObjectSequentialID& Other) const { return !(*this == Other); }

	bool IsValid() const { return Value != UE::SmartObject::InvalidID; }

	UE::SmartObject::ID GetValue() const { return Value; }

	FString Describe() const { return FString::Printf(TEXT("Id[%d]"), Value); }

 	friend uint32 GetTypeHash(const FSmartObjectSequentialID SmartObjectID)
 	{
 		return SmartObjectID.GetValue();
 	}

private:
	UE::SmartObject::ID Value;

public:
	static const FSmartObjectSequentialID Invalid;
};

typedef FSmartObjectSequentialID FSmartObjectUserID;


/**
 * Helper struct to wrap basic functionalities to store and use smartobject unique IDs.
 * Those IDs are assigned in editor by the collection and then serialized for runtime.
 */
USTRUCT(BlueprintType)
struct SMARTOBJECTSMODULE_API FSmartObjectID
{
	GENERATED_BODY()

public:
	FSmartObjectID() {}

	bool IsValid() const { return *this != Invalid; }
	void Reset() { *this = Invalid; }

	FString Describe() const { return LexToString(ID); }

	bool operator==(const FSmartObjectID& Other) const { return ID == Other.ID; }
	bool operator!=(const FSmartObjectID& Other) const { return !(*this == Other); }

	friend uint32 GetTypeHash(const FSmartObjectID& SmartObjectID)
	{
		return SmartObjectID.ID;
	}

private:
	/** Valid Id must be created by the collection */
 	friend class ASmartObjectCollection;

	explicit FSmartObjectID(const uint32 InID) : ID(InID) {}

	UPROPERTY(VisibleAnywhere, Category = SmartObject)
	uint32 ID = INDEX_NONE;

 public:
 	static const FSmartObjectID Invalid;
};