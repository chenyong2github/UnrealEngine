// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "BlueprintHeaderViewSettings.generated.h"

UENUM()
enum class EHeaderViewSortMethod : uint8
{
	None,
	SortByAccessSpecifier
};

USTRUCT(NotBlueprintable)
struct FHeaderViewSyntaxColors
{
	GENERATED_BODY()
	
public:
	UPROPERTY(config, EditAnywhere, Category="Settings")
	FLinearColor Comment = FLinearColor(0.3f, 0.7f, 0.1f, 1.0f);
	
	UPROPERTY(config, EditAnywhere, Category="Settings")
	FLinearColor Error = FLinearColor(1.0f, 0.0f, 0.0f, 1.0f);
	
	UPROPERTY(config, EditAnywhere, Category="Settings")
	FLinearColor Macro = FLinearColor(0.6f, 0.2f, 0.8f, 1.0f);
	
	UPROPERTY(config, EditAnywhere, Category="Settings")
	FLinearColor Typename = FLinearColor::White;
	
	UPROPERTY(config, EditAnywhere, Category="Settings")
	FLinearColor Identifier = FLinearColor::White;
	
	UPROPERTY(config, EditAnywhere, Category="Settings")
	FLinearColor Keyword = FLinearColor(0.0f, 0.4f, 0.8f, 1.0f);
	
};

/** Settings for the Blueprint Header View Plugin */
UCLASS(config = EditorPerProjectUserSettings)
class BLUEPRINTHEADERVIEW_API UBlueprintHeaderViewSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UBlueprintHeaderViewSettings();

	//~ Begin UDeveloperSettings interface
	virtual FName GetCategoryName() const override;
	virtual FText GetSectionText() const override;
	virtual FName GetSectionName() const override;
	//~ End UDeveloperSettings interface

public:
	
	/** Syntax Highlighting colors for Blueprint Header View output */
	UPROPERTY(config, EditAnywhere, Category="Settings")
	FHeaderViewSyntaxColors SyntaxColors;

	/** Sorting Method for Header View Functions and Properties */
	UPROPERTY(config, EditAnywhere, Category="Settings")
	EHeaderViewSortMethod SortMethod = EHeaderViewSortMethod::None;
};
