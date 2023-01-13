// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"

#include "PCGCommon.h"

#include "PCGData.generated.h"

class UPCGSettings;
class UPCGSettingsInterface;
class UPCGParamData;

/**
* Base class for any "data" class in the PCG framework.
* This is an intentionally vague base class so we can have the required
* flexibility to pass in various concrete data types, settings, and more.
*/
UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGData : public UObject
{
	GENERATED_BODY()

public:
	virtual EPCGDataType GetDataType() const { return EPCGDataType::None; }
};

USTRUCT(BlueprintType)
struct PCG_API FPCGTaggedData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Data)
	TObjectPtr<const UPCGData> Data = nullptr;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Data)
	TSet<FString> Tags;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Data)
	FName Pin = NAME_None;

	bool operator==(const FPCGTaggedData& Other) const;
	bool operator!=(const FPCGTaggedData& Other) const;
};

struct FPCGRootSet
{
	~FPCGRootSet() { Clear(); }
	void Clear();
	void Add(UObject* InObject);
	void Remove(UObject* InObject);

	TMap<UObject*, int32> RootSet;
private:
	void AddInternal(UObject* InObject);
	void RemoveInternal(UObject* InObject);
};

USTRUCT(BlueprintType)
struct PCG_API FPCGDataCollection
{
	GENERATED_BODY()

	/** Returns all spatial data in the collection */
	TArray<FPCGTaggedData> GetInputs() const;
	/** Returns all data on a given pin */
	TArray<FPCGTaggedData> GetInputsByPin(const FName& InPinLabel) const;
	/** Returns all spatial data in the collection with the given tag */
	TArray<FPCGTaggedData> GetTaggedInputs(const FString& InTag) const;
	/** Returns all settings in the collection */
	TArray<FPCGTaggedData> GetAllSettings() const;
	/** Returns all params in the collection */
	TArray<FPCGTaggedData> GetAllParams() const;
	/** Returns all params in the collection with a given tag */
	TArray<FPCGTaggedData> GetTaggedParams(const FString& InTag) const;
	/** Returns all params on a given pin */
	TArray<FPCGTaggedData> GetParamsByPin(const FName& InPinLabel) const;
	/** Returns the first params found in the collection */
	UPCGParamData* GetParams() const; 
	/** Returns the first/only param found on the default params pin */
	UPCGParamData* GetParamsOnParamsPin() const;

	const UPCGSettingsInterface* GetSettingsInterface() const;
	const UPCGSettingsInterface* GetSettingsInterface(const UPCGSettingsInterface* InDefaultSettingsInterface) const;

	template<typename SettingsType>
	const SettingsType* GetSettings() const;

	const UPCGSettings* GetSettings(const UPCGSettings* InDefaultSettings) const;

	bool operator==(const FPCGDataCollection& Other) const;
	bool operator!=(const FPCGDataCollection& Other) const;
	void AddToRootSet(FPCGRootSet& RootSet) const;
	void RemoveFromRootSet(FPCGRootSet& RootSet) const;

	/** Cleans up the collection, but does not unroot any previously rooted data. */
	void Reset();

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Data)
	TArray<FPCGTaggedData> TaggedData;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Data)
	bool bCancelExecutionOnEmpty = false;

	/** This flag is used to cancel further computation or for the debug/isolate feature */
	bool bCancelExecution = false;
};

template<typename SettingsType>
inline const SettingsType* FPCGDataCollection::GetSettings() const
{
	const FPCGTaggedData* MatchingData = TaggedData.FindByPredicate([](const FPCGTaggedData& Data) {
		return Cast<const SettingsType>(Data.Data) != nullptr;
		});

	return MatchingData ? Cast<const SettingsType>(MatchingData->Data) : nullptr;
}

UCLASS()
class PCG_API UPCGDataFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Blueprint methods to support interaction with FPCGDataCollection
	UFUNCTION(BlueprintCallable, Category = Data)
	static TArray<FPCGTaggedData> GetInputs(const FPCGDataCollection& InCollection);

	UFUNCTION(BlueprintCallable, Category = Data)
	static TArray<FPCGTaggedData> GetInputsByPin(const FPCGDataCollection& InCollection, const FName& InPinLabel);

	UFUNCTION(BlueprintCallable, Category = Data)
	static TArray<FPCGTaggedData> GetTaggedInputs(const FPCGDataCollection& InCollection, const FString& InTag);

	UFUNCTION(BlueprintCallable, Category = Data)
	static TArray<FPCGTaggedData> GetParams(const FPCGDataCollection& InCollection);

	UFUNCTION(BlueprintCallable, Category = Data)
	static TArray<FPCGTaggedData> GetParamsByPin(const FPCGDataCollection& InCollection, const FName& InPinLabel);

	UFUNCTION(BlueprintCallable, Category = Data)
	static TArray<FPCGTaggedData> GetTaggedParams(const FPCGDataCollection& InCollection, const FString& InTag);

	UFUNCTION(BlueprintCallable, Category = Data)
	static TArray<FPCGTaggedData> GetAllSettings(const FPCGDataCollection& InCollection);
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#include "PCGModule.h"
#endif
