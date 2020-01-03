// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SelectionSystem/DataprepFilter.h"

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "DataprepFloatFilter.generated.h"

class UDataprepFloatFetcher;

UENUM()
enum class EDataprepFloatMatchType : uint8
{
	LessThan,
	GreatherThan,
	IsNearlyEqual
};

UCLASS()
class DATAPREPCORE_API UDataprepFloatFilter : public UDataprepFilter
{
	GENERATED_BODY()

public:
	bool Filter(float Float) const;

	//~ Begin UDataprepFilter Interface
	virtual TArray<UObject*> FilterObjects(const TArray<UObject*>& Objects) const override;
	virtual bool IsThreadSafe() const override { return true; }
	virtual FText GetFilterCategoryText() const override;
	virtual TSubclassOf<UDataprepFetcher> GetAcceptedFetcherClass() const override;
	virtual void SetFetcher(const TSubclassOf<UDataprepFetcher>& FetcherClass) override;
	virtual UDataprepFetcher* GetFetcher() const override;
	//~ Begin UDataprepFilter Interface

	EDataprepFloatMatchType GetFloatMatchingCriteria() const;
	float GetEqualValue() const;
	float GetTolerance() const;

	void SetFloatMatchingCriteria(EDataprepFloatMatchType FloatMatchingCriteria);
	void SetEqualValue(float EqualValue);
	void SetTolerance(float Tolerance);

private:
	// The source of float selected by the user
	UPROPERTY()
	UDataprepFloatFetcher* FloatFetcher;

	// The criteria selected by the user
	UPROPERTY(EditAnywhere, Category = Filter)
	EDataprepFloatMatchType FloatMatchingCriteria;

	// The value to use for the equality check
	UPROPERTY(EditAnywhere, Category = Filter)
	float EqualValue;

	// The value used for the tolerance when doing a nearly equal
	UPROPERTY(EditAnywhere, Category = Filter)
	float Tolerance = KINDA_SMALL_NUMBER;
};
