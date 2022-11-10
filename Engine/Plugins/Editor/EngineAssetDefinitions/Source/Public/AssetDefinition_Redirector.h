// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetDefinitionDefault.h"
#include "UObject/ObjectRedirector.h"

#include "AssetDefinition_Redirector.generated.h"

UCLASS()
class ENGINEASSETDEFINITIONS_API UAssetDefinition_Redirector : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	// UAssetDefinition Implementation
	virtual FText GetAssetDisplayName() const override { return NSLOCTEXT("AssetDefinition", "Redirector", "Redirector"); }
	virtual FLinearColor GetAssetColor() const override { return FLinearColor(FColor(128, 128, 128)); }
	virtual TSoftClassPtr<UObject> GetAssetClass() const override { return UObjectRedirector::StaticClass(); }
	virtual EAssetCommandResult ActivateAssets(const FAssetActivateArgs& ActivateArgs) const override;
};
