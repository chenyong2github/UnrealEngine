// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "PCGData.generated.h"

class UPCGSettings;

/**
* Base class for any "data" class in the PCG framework.
* This is an intentionally vague base class so we can have the required
* flexibility to pass in various concrete data types, settings, and more.
*/
UCLASS(BlueprintType, ClassGroup = (Procedural))
class UPCGData : public UObject
{
	GENERATED_BODY()
};

UENUM(BlueprintType)
enum class EPCGDataUsage : uint8
{
	Input, // must be castable to concrete data
	Exclusion,
	Settings,
	Override,
	// Manual? Blocker?
};

USTRUCT(BlueprintType)
struct FPCGTaggedData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Data)
	TObjectPtr<const UPCGData> Data;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Data)
	EPCGDataUsage Usage = EPCGDataUsage::Input;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Data)
	TSet<FString> Tags;
};

USTRUCT(BlueprintType)
struct FPCGDataCollection
{
	GENERATED_BODY()

	TArray<FPCGTaggedData> GetInputs() const;
	TArray<FPCGTaggedData> GetExclusions() const;
	TArray<FPCGTaggedData> GetAllSettings() const;

	template<typename SettingsType>
	const SettingsType* GetSettings() const;

	const UPCGSettings* GetSettings(const UPCGSettings* InDefaultSettings) const;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Data)
	TArray<FPCGTaggedData> TaggedData;
};

template<typename SettingsType>
inline const SettingsType* FPCGDataCollection::GetSettings() const
{
	const FPCGTaggedData* MatchingData = TaggedData.FindByPredicate([](const FPCGTaggedData& Data) {
		return Data.Usage == EPCGDataUsage::Settings && Cast<const SettingsType>(Data.Data) != nullptr;
		});

	return MatchingData ? Cast<const SettingsType>(MatchingData->Data) : nullptr;
}

UCLASS()
class UPCGDataFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Blueprint methods to support interaction with FPCGDataCollection
	UFUNCTION(BlueprintCallable, Category = Data)
	static TArray<FPCGTaggedData> GetInputs(const FPCGDataCollection& InCollection);

	UFUNCTION(BlueprintCallable, Category = Data)
	static TArray<FPCGTaggedData> GetExclusions(const FPCGDataCollection& InCollection);

	UFUNCTION(BlueprintCallable, Category = Data)
	static TArray<FPCGTaggedData> GetAllSettings(const FPCGDataCollection& InCollection);
};
