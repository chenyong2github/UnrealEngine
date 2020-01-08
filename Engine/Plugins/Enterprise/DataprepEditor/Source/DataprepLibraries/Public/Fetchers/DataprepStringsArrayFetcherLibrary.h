// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SelectionSystem/DataprepStringsArrayFetcher.h"

#include "CoreMinimal.h"

#include "DataprepStringsArrayFetcherLibrary.generated.h"

UCLASS(BlueprintType, NotBlueprintable, Meta = (DisplayName = "Actor Tag", ToolTip = "Return the tags of an actor."))
class UDataprepStringActorTagsFetcher final : public UDataprepStringsArrayFetcher
{
	GENERATED_BODY()

public:
	//~ UDataprepStringArrayFetcher interface
	virtual TArray<FString> Fetch_Implementation(const UObject* Object, bool& bOutFetchSucceded) const override;
	//~ End of UDataprepStringFetcher interface

	//~ UDataprepFetcher interface
	virtual bool IsThreadSafe() const final;
	virtual FText GetNodeDisplayFetcherName_Implementation() const;
	//~ End of UDataprepFetcher interface
};
