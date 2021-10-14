// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ContentBrowserItem.h"
#include "TextFilterValueHandler.h"

#include "TextFilterValueHandlers.generated.h"

UCLASS(transient, config = Editor)
class UTextFilterValueHandlers : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(config)
	TArray<TSoftClassPtr<UTextFilterValueHandler>> TextFilterValueHandlers;

	static bool HandleTextFilterValue(const FContentBrowserItem& InContentBrowserItem, const FTextFilterString& InValue, const ETextFilterTextComparisonMode InTextComparisonMode, bool& bOutIsMatch);
};