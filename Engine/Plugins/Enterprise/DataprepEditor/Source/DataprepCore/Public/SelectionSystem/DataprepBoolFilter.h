// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SelectionSystem/DataprepFilter.h"

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "DataprepBoolFilter.generated.h"

class UDataprepBoolFetcher;

UCLASS()
class DATAPREPCORE_API UDataprepBoolFilter : public UDataprepFilter
{
	GENERATED_BODY()

public:
	bool Filter(const bool bResult) const;

	//~ Begin UDataprepFilter Interface
	virtual TArray<UObject*> FilterObjects(const TArray<UObject*>& Objects) const override;
	virtual bool IsThreadSafe() const override { return true; }
	virtual FText GetFilterCategoryText() const override;
	virtual TSubclassOf<UDataprepFetcher> GetAcceptedFetcherClass() const override;
	virtual void SetFetcher(const TSubclassOf<UDataprepFetcher>& FetcherClass) override;
	virtual UDataprepFetcher* GetFetcher() const override;
	//~ Begin UDataprepFilter Interface

private:
	UPROPERTY()
	UDataprepBoolFetcher* BoolFetcher;
};
