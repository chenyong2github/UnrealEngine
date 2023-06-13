// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "GameFeatureVersePathMapperCommandlet.generated.h"

class FAssetRegistryState;

USTRUCT()
struct FJsonVersePathGfpMapEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FString VersePath;

	UPROPERTY()
	TArray<FString> GfpUriList;
};

USTRUCT()
struct FJsonVersePathGfpMap
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FJsonVersePathGfpMapEntry> MapEntries;
};

UCLASS(config = Editor)
class UGameFeatureVersePathMapperCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	static GAMEFEATURES_API TOptional<TMap<FString, TArray<FString>>> BuildLookup(const ITargetPlatform* TargetPlatform = nullptr, const FAssetRegistryState* DevAR = nullptr);

	virtual int32 Main(const FString& CmdLineParams) override;
};
