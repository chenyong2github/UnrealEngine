// Copyright Epic Games, Inc. All Rights Reserved.
#pragma  once

#include "AssetRegistry/AssetData.h"
#include "CoreMinimal.h"
#include "RemoteControlField.h"
#include "RemoteControlModels.h"
#include "RemoteControlPreset.h"

#include "RemoteControlResponse.generated.h"

USTRUCT()
struct FListPresetsResponse
{
	GENERATED_BODY()
	
	FListPresetsResponse() = default;

	FListPresetsResponse(const TArray<TSoftObjectPtr<URemoteControlPreset>>& InPresets)
	{
		Presets.Append(InPresets);
	}
	
	UPROPERTY()
	TArray<FRCShortPresetDescription> Presets;
};

USTRUCT()
struct FGetPresetResponse
{
	GENERATED_BODY()

	FGetPresetResponse() = default;

	FGetPresetResponse(const URemoteControlPreset* InPreset)
		: Preset(InPreset)
	{}

	UPROPERTY()
	FRCPresetDescription Preset;
};

USTRUCT()
struct FDescribeObjectResponse
{
	GENERATED_BODY()

	FDescribeObjectResponse() = default;

	FDescribeObjectResponse(UObject* Object)
		: Name(Object->GetName())
		, Class(Object->GetClass())
	{
		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_NativeAccessSpecifierProtected | CPF_NativeAccessSpecifierPrivate | CPF_DisableEditOnInstance))
			{
				Properties.Emplace(*It);
			}
		}

		for (TFieldIterator<UFunction> It(Object->GetClass()); It; ++It)
		{
			if (It->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_Public))
			{
				Functions.Emplace(*It);
			}
		}
	}

	UPROPERTY()
	FString Name;

	UPROPERTY()
	UClass* Class = nullptr;

	UPROPERTY()
	TArray<FRCPropertyDescription> Properties;

	UPROPERTY()
	TArray<FRCFunctionDescription> Functions;
};


USTRUCT()
struct FSearchAssetResponse
{
	GENERATED_BODY()

	FSearchAssetResponse() = default;

	FSearchAssetResponse(const TArray<FAssetData>& InAssets)
	{
		Assets.Append(InAssets);
	}

	UPROPERTY()
	TArray<FRCAssetDescription> Assets;
};


USTRUCT()
struct FSearchActorResponse
{
	GENERATED_BODY()

	FSearchActorResponse() = default;

	FSearchActorResponse(const TArray<AActor*>& InActors)
	{
		Actors.Append(InActors);
	}

	UPROPERTY()
	TArray<FRCActorDescription> Actors;
};

USTRUCT()
struct FGetMetadataFieldResponse
{
	GENERATED_BODY()

	FGetMetadataFieldResponse() = default;

	FGetMetadataFieldResponse(FString InValue)
		: Value(MoveTemp(InValue))
	{}

	UPROPERTY()
	FString Value;
};


USTRUCT()
struct FGetMetadataResponse
{
	GENERATED_BODY()

	FGetMetadataResponse() = default;

	FGetMetadataResponse(TMap<FString, FString> InMetadata)
		: Metadata(MoveTemp(InMetadata))
	{}

	UPROPERTY()
	TMap<FString, FString> Metadata;
};

USTRUCT()
struct FRCPresetFieldsRenamedEvent
{
	GENERATED_BODY()

	FRCPresetFieldsRenamedEvent() = default;

	FRCPresetFieldsRenamedEvent(FName InPresetName, TArray<TTuple<FName, FName>> InRenamedFields)
		: Type(TEXT("PresetFieldsRenamed"))
		, PresetName(InPresetName)
		, RenamedFields(MoveTemp(InRenamedFields))
	{
	}

	UPROPERTY()
	FString Type;

	UPROPERTY()
	FName PresetName;

	UPROPERTY()
	TArray<FRCPresetFieldRenamed> RenamedFields;
};

USTRUCT()
struct FRCPresetFieldsRemovedEvent
{
	GENERATED_BODY()

	FRCPresetFieldsRemovedEvent() = default;

	FRCPresetFieldsRemovedEvent(FName InPresetName, TArray<FName> InRemovedFields)
		: Type(TEXT("PresetFieldsRemoved"))
		, PresetName(InPresetName)
		, RemovedFields(MoveTemp(InRemovedFields))
	{
	}

	UPROPERTY()
	FString Type;

	UPROPERTY()
	FName PresetName;

	UPROPERTY()
	TArray<FName> RemovedFields;
};

USTRUCT()
struct FRCPresetFieldsAddedEvent
{
	GENERATED_BODY()

	FRCPresetFieldsAddedEvent() = default;

	FRCPresetFieldsAddedEvent(FName InPresetName, FRCPresetDescription InPresetAddition)
		: Type(TEXT("PresetFieldsAdded"))
		, PresetName(InPresetName)
		, Description(MoveTemp(InPresetAddition))
	{
	}

	UPROPERTY()
	FString Type;

	UPROPERTY()
	FName PresetName;

	UPROPERTY()
	FRCPresetDescription Description;
};
