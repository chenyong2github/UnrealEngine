// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DMXNameListItem.h"
#include "Misc/Crc.h"
#include <UObject/NameTypes.h>

#include "Kismet/BlueprintFunctionLibrary.h"

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

	TArray<FString> GetKeywords() const;

	// cleanup the list of keywords using commas and removing spaces/tabs
	void CleanupKeywords();
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

public:
	//~ Constructors

	/**
	 * Default constructor. Assigns Attribute to the first available
	 * Attribute from the plugin settings
	 */
	FDMXAttributeName();

	/** Construct from an Attribute */
	explicit FDMXAttributeName(const FDMXAttribute& InAttribute);

	/** Construct from an Attribute name */
	FDMXAttributeName(const FName& NameAttribute);

	//~ FDMXNameListItem interface
	virtual void SetFromName(const FName& InName) override;

	const FDMXAttribute& GetAttribute() const;
	operator const FDMXAttribute& () const { return GetAttribute(); }
};

inline uint32 GetTypeHash(const FDMXAttributeName& DMXNameListItem)
{
	FString NameStr = DMXNameListItem.Name.ToString();

	return FCrc::MemCrc32(*NameStr, sizeof(TCHAR) * NameStr.Len());
}

inline bool operator==(const FDMXAttributeName& V1, const FDMXAttributeName& V2)
{
	return V1.Name.IsEqual(V2.Name);
}

inline bool operator!=(const FDMXAttributeName& V1, const FDMXAttributeName& V2)
{
	return !V1.Name.IsEqual(V2.Name);
}

UCLASS()
class UDMXAttributeNameConversions
	: public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, meta = (DisplayName = "ToString (DMX Attribute)", CompactNodeTitle = "->", BlueprintAutocast), Category = "Utilities|DMX")
	static FString Conv_DMXAttributeToString(const FDMXAttributeName& InAttribute);

	UFUNCTION(BlueprintPure, meta = (DisplayName = "ToName (DMX Attribute)", CompactNodeTitle = "->", BlueprintAutocast), Category = "Utilities|DMX")
	static FName Conv_DMXAttributeToName(const FDMXAttributeName& InAttribute);
};
