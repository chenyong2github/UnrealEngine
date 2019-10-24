// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "TemplateProjectDefs.h"

/** Struct describing a single template project */
struct FTemplateItem
{
	FText		Name;
	FText		Description;
	TArray<FName> Categories;

	FString		Key;
	FString		SortKey;

	TSharedPtr<FSlateBrush> Thumbnail;
	TSharedPtr<FSlateBrush> PreviewImage;

	FString		ClassTypes;
	FString		AssetTypes;

	FString		CodeProjectFile;
	FString		BlueprintProjectFile;

	TArray<ETemplateSetting> HiddenSettings;

	bool		bIsEnterprise;
	bool		bIsBlankTemplate;
};
