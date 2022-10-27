// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/IToolkitHost.h"
#include "AssetTypeActions_Base.h"
#include "NiagaraEditorModule.h"

class FAssetTypeActions_NiagaraValidationRuleSet : public FAssetTypeActions_Base
{
public:
	// IAssetTypeActions Implementation
	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override;
	virtual UClass* GetSupportedClass() const override;
	virtual bool CanFilter() override { return true; }
	virtual uint32 GetCategories() override { return FNiagaraEditorModule::GetAssetCategory(); }
	virtual const TArray<FText>& GetSubMenus() const override;
};
