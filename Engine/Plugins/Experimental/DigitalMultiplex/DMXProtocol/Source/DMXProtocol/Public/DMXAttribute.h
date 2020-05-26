// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DMXNameListItem.h"

#include "DMXAttribute.generated.h"

USTRUCT()
struct DMXPROTOCOL_API FDMXAttribute
{
	GENERATED_BODY()

	/** Name of this Attribute, displayed on Attribute selectors */
	UPROPERTY(EditDefaultsOnly, Category = "DMX")
	FName Name;

	/**
	 * Keywords used when auto-mapping Fixture Functions from a GDTF file to
	 * match Fixture Functions to existing Attributes.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "DMX")
	FString Keywords;

	/** Comparison operator */
	FORCEINLINE bool operator==(const FDMXAttribute& Other) const
	{
		return Name.IsEqual(Other.Name) && Keywords.Equals(Other.Keywords);
	}
	FORCEINLINE bool operator!=(const FDMXAttribute& Other) const { return !(*this == Other); }
};

/** Unique hash from a DMX Attribute */
FORCEINLINE uint32 GetTypeHash(const FDMXAttribute& Attribute)
{
	return GetTypeHash(Attribute.Name);
}

USTRUCT(BlueprintType, Category = "DMX")
struct DMXPROTOCOL_API FDMXAttributeName
	: public FDMXNameListItem
{
	GENERATED_BODY()

	DECLARE_DMX_NAMELISTITEM_STATICS(true)

	//~ Constructors

	/**
	 * Default constructor. Assigns Attribute to the first available
	 * Attribute from the plugin settings
	 */
	FDMXAttributeName();

	/** Construct from an Attribute */
	explicit FDMXAttributeName(const FDMXAttribute& InAttribute);

	/** Construct from an Attribute name */
	FDMXAttributeName(const FName& AttributeName);

	//~ FDMXNameListItem interface
	virtual void SetFromName(const FName& InName) override;

	const FDMXAttribute& GetAttribute() const;
	operator const FDMXAttribute& () const { return GetAttribute(); }

	//~ Comparison operators
	FORCEINLINE bool operator==(const FDMXAttributeName& Other) const { return Name.IsEqual(Name); }
	FORCEINLINE bool operator!=(const FDMXAttributeName& Other) const { return !(*this == Other); }
	FORCEINLINE bool operator==(const FDMXAttribute& Other) const { return Name.IsEqual(Other.Name); }
	FORCEINLINE bool operator!=(const FDMXAttribute& Other) const { return !Name.IsEqual(Other.Name); }
	FORCEINLINE bool operator==(const FName& Other) const { return Name.IsEqual(Other); }
	FORCEINLINE bool operator!=(const FName& Other) const { return Name.IsEqual(Other); }
};
