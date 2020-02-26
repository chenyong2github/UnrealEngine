// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GroomSettings.h"
#include "UObject/Object.h"
#include "GroomImportOptions.generated.h"

UCLASS(BlueprintType, config = EditorPerProjectUserSettings, HideCategories = ("Hidden"))
class HAIRSTRANDSCORE_API UGroomImportOptions : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, meta = (ShowOnlyInnerProperties), Category = Conversion)
	FGroomConversionSettings ConversionSettings;

	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, meta = (ShowOnlyInnerProperties), Category = BuildSettings)
	FGroomBuildSettings BuildSettings;
};

USTRUCT(BlueprintType)
struct HAIRSTRANDSCORE_API FGroomHairGroupPreview
{
	GENERATED_USTRUCT_BODY()

	FGroomHairGroupPreview()
	: GroupID(0)
	, CurveCount(0)
	, GuideCount(0)
	{}

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = Preview)
	int32 GroupID;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = Preview)
	int32 CurveCount;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = Preview)
	int32 GuideCount;
};

UCLASS(BlueprintType, config = EditorPerProjectUserSettings, HideCategories = ("Hidden"))
class HAIRSTRANDSCORE_API UGroomHairGroupsPreview : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY(config, VisibleAnywhere, BlueprintReadWrite, meta = (ShowOnlyInnerProperties), Category = Preview)
	TArray<FGroomHairGroupPreview> Groups;
};