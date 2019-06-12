// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewModels/Stack/NiagaraStackSpacer.h"
#include "NiagaraStackScriptItemGroup.h"
#include "AssetData.h"
#include "NiagaraStackModuleSpacer.generated.h"

UCLASS()
class NIAGARAEDITOR_API UNiagaraStackModuleSpacer : public UNiagaraStackSpacer
{
	GENERATED_BODY()

public:

	DECLARE_DELEGATE_TwoParams(FOnStackSpacerAcceptDrop, const UNiagaraStackModuleSpacer*, const FNiagaraVariable&)
	DECLARE_DELEGATE_TwoParams(FOnStackSpacerAssetDrop, const UNiagaraStackModuleSpacer*, const FAssetData&)
	DECLARE_DELEGATE_RetVal_TwoParams(bool, FOnStackSpacerRequestAssetDrop, const UNiagaraStackModuleSpacer*, const FAssetData&)

	void Initialize(FRequiredEntryData InRequiredEntryData, ENiagaraScriptUsage InScriptUsage, FName SpacerKey = NAME_None, float InSpacerScale = 1.0f, EStackRowStyle InRowStyle = UNiagaraStackEntry::EStackRowStyle::None);

	//~ DragDropHandling
	virtual FReply OnStackSpacerDrop(TSharedPtr<FDragDropOperation> DragDropOperation) override; 
	virtual bool OnStackSpacerAllowDrop(TSharedPtr<FDragDropOperation> DragDropOperation) override;

	FOnStackSpacerAcceptDrop OnStackSpacerAcceptDrop;

	FOnStackSpacerRequestAssetDrop OnStackSpacerRequestAssetDrop;
	FOnStackSpacerAssetDrop OnStackSpacerAssetDrop;

private:
	ENiagaraScriptUsage ItemGroupScriptUsage;
}; 
