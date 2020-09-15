// Copyright Epic Games, Inc. All Rights Reserved.
#pragma  once

#include "CoreMinimal.h"
#include "RemoteControlModels.h"
#include "RemoteControlPreset.h"
#include "RemoteControlField.h"
#include "Algo/Transform.h"
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
		Algo::Transform(InAssets, Assets, [] (const FAssetData& Asset) { return Asset; } );
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
		Algo::Transform(InActors, Actors, [](const AActor* Actor) { return Actor; });
	}

	UPROPERTY()
	TArray<FRCActorDescription> Actors;
};
