// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ContentBrowserItem.h"
#include "TextFilterKeyValueHandler.h"

#include "TextFilterKeyValueHandlers.generated.h"

USTRUCT()
struct FTextFilterKeyValueHandlerEntry
{
	GENERATED_BODY()

	UPROPERTY(config)
	FName Key;

	UPROPERTY(config)
	TSoftClassPtr<UTextFilterKeyValueHandler> HandlerClass;
};

UCLASS(transient, config = Editor)
class UTextFilterKeyValueHandlers : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(config)
	TArray<FTextFilterKeyValueHandlerEntry> TextFilterKeyValueHandlers;

	static bool HandleTextFilterKeyValue(const FContentBrowserItem& InContentBrowserItem, const FName& InKey, const FTextFilterString& InValue, const ETextFilterComparisonOperation InComparisonOperation, const ETextFilterTextComparisonMode InTextComparisonMode);
};