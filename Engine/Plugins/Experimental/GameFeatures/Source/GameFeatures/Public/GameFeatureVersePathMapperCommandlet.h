// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "GameFeatureVersePathMapperCommandlet.generated.h"

USTRUCT()
struct FVersePathGfpMapEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FString VersePath;

	UPROPERTY()
	TSet<FString> GfpUriList;

	UPROPERTY()
	TSet<FString> GfpUriDependencyList;
};

USTRUCT()
struct FVersePathGfpMap
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FVersePathGfpMapEntry> MapEntries;
};

UCLASS(config = Editor)
class UGameFeatureVersePathMapperCommandlet : public UCommandlet
{
	GENERATED_BODY()

	virtual int32 Main(const FString& CmdLineParams) override;
};
