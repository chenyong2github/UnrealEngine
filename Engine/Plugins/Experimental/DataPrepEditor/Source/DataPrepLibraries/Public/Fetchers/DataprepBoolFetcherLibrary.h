// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SelectionSystem/DataprepBoolFetcher.h"

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "UObject/Object.h"

#include "DataprepBoolFetcherLibrary.generated.h"

UCLASS(BlueprintType, NotBlueprintable, Meta = (DisplayName="Is Class Of", ToolTip = "Return true when a object is of the selected class."))
class UDataprepIsClassOfFetcher final : public UDataprepBoolFetcher
{
	GENERATED_BODY()
public:
	//~ UDataprepBoolFetcher interface
	virtual bool Fetch_Implementation(const UObject* Object, bool& bOutFetchSucceded) const final;
	//~ End of UDataprepFloatFetcher interface

	//~ UDataprepFetcher interface
	virtual bool IsThreadSafe() const final;
	virtual FText GetAdditionalKeyword_Implementation() const;
	//~ End of UDataprepFetcher interface

	// The key for the for the string
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Settings")
	TSubclassOf<UObject> Class;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Settings")
	bool bShouldIncludeChildClass = true;

	static FText AdditionalKeyword;
};

UCLASS(BlueprintType, NotBlueprintable, Meta = (DisplayName = "Has Actor Tag", ToolTip = "Return true when a actor has the specified tag."))
class UDataprepHasActorTagFetcher final : public UDataprepBoolFetcher
{
	GENERATED_BODY()
public:
	//~ UDataprepBoolFetcher interface
	virtual bool Fetch_Implementation(const UObject* Object, bool& bOutFetchSucceded) const final;
	//~ End of UDataprepFloatFetcher interface

	//~ UDataprepFetcher interface
	virtual bool IsThreadSafe() const final;
	//~ End of UDataprepFetcher interface

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	FName Tag;
};
