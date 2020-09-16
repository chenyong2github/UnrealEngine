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

	FListPresetsResponse(const TArray<URemoteControlPreset*>& InPresets)
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
	UClass* Class;

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
