// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SelectionSystem/DataprepStringFetcher.h"

#include "CoreMinimal.h"

#include "DatasmithDataprepFetcher.generated.h"

UCLASS(BlueprintType, NotBlueprintable, Meta = (DisplayName="Metadata Value", ToolTip="Collect the value for a key from the datasmith user metadata."))
class UDatasmithStringMetadataValueFetcher final : public UDataprepStringFetcher
{
	GENERATED_BODY()
public:
	//~ UDataprepStringFetcher interface
	virtual FString Fetch_Implementation(const UObject* Object, bool& bOutFetchSucceded) const final;
	//~ End of UDataprepStringFetcher interface

	//~ UDataprepFetcher interface
	virtual FText GetNodeDisplayFetcherName_Implementation() const;
	virtual bool IsThreadSafe() const final;
	//~ End of UDataprepFetcher interface


	// The key for the for the string
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Key")
	FName Key;
};
