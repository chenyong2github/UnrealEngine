// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetDefinitionDefault.h"
#include "UObject/ObjectRedirector.h"

#include "AssetDefinition_Redirector.generated.h"

struct FToolMenuContext;

UCLASS()
class UAssetDefinition_Redirector : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	// UAssetDefinition Implementation
	virtual FText GetAssetDisplayName() const override { return NSLOCTEXT("AssetDefinition", "Redirector", "Redirector"); }
	virtual FLinearColor GetAssetColor() const override { return FLinearColor(FColor(128, 128, 128)); }
	virtual TSoftClassPtr<UObject> GetAssetClass() const override { return UObjectRedirector::StaticClass(); }
	virtual EAssetCommandResult ActivateAssets(const FAssetActivateArgs& ActivateArgs) const override;

	//virtual uint32 GetCategories() override { return EAssetTypeCategories::Misc; }

protected:
	virtual void OnRegistered() override;

private:
	void RegisterMenus();
	
	/** Handler for when FindTarget is selected */
	void ExecuteFindTarget(const FToolMenuContext& MenuContext);

	/** Handler for when FixUp is selected */
	void ExecuteFixUp(const FToolMenuContext& MenuContext, bool bDeleteAssets);

	/** Syncs the content browser to the destination objects for all the supplied redirectors */
	void FindTargets(const TArray<UObjectRedirector*>& Redirectors) const;
};
