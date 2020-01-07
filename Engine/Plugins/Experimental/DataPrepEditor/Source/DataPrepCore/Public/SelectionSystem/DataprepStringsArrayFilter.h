// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SelectionSystem/DataprepStringFilter.h"

#include "DataprepStringsArrayFilter.generated.h"

class UDataprepStringsArrayFetcher;

UCLASS()
class DATAPREPCORE_API UDataprepStringsArrayFilter : public UDataprepFilter
{
	GENERATED_BODY()

public:
	bool Filter(const TArray<FString>& StringArray) const;

	//~ Begin UDataprepFilter Interface
	virtual TArray<UObject*> FilterObjects(const TArray<UObject*>& Objects) const override;
	virtual bool IsThreadSafe() const override { return true; }
	virtual FText GetFilterCategoryText() const override;
	virtual TSubclassOf<UDataprepFetcher> GetAcceptedFetcherClass() const override;
	virtual void SetFetcher(const TSubclassOf<UDataprepFetcher>& FetcherClass) override;
	virtual UDataprepFetcher* GetFetcher() const override;
	//~ Begin UDataprepFilter Interface

	EDataprepStringMatchType GetStringMatchingCriteria() const;
	FString GetUserString() const;

	void SetStringMatchingCriteria(EDataprepStringMatchType StringMatchingCriteria);
	void SetUserString(FString UserString);

private:
	// The criteria selected by the user
	UPROPERTY(EditAnywhere, Category = Filter)
	EDataprepStringMatchType StringMatchingCriteria;

	// The string entered by the user
	UPROPERTY(EditAnywhere, Category = Filter)
	FString UserString;

	// The source of string selected by the user
	UPROPERTY()
	UDataprepStringsArrayFetcher* StringsArrayFetcher;
};
