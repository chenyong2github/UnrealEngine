// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "UObject/ObjectMacros.h"
#include "BlutilityMenuExtensions.generated.h"

struct FAssetData;
class FMenuBuilder;

// Blutility Menu extension helpers
class FBlutilityMenuExtensions
{
public:
	/** Helper function to get all Blutility classes derived from the specified class name */
	static void GetBlutilityClasses(TArray<FAssetData>& OutAssets, const FName& InClassName);

	/** Helper function that populates a menu based on the exposed functions in a set of Blutility objects */
	static void CreateBlutilityActionsMenu(FMenuBuilder& MenuBuilder, TArray<class IEditorUtilityExtension*> Utils);
};

UINTERFACE(BlueprintType)
class UEditorUtilityExtension : public UInterface
{
	GENERATED_UINTERFACE_BODY()
};

class IEditorUtilityExtension
{
	GENERATED_IINTERFACE_BODY()
};
