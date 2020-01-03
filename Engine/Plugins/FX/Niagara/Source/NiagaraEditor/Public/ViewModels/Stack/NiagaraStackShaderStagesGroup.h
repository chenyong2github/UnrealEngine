// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewModels/Stack/NiagaraStackItemGroup.h"
#include "NiagaraStackShaderStagesGroup.generated.h"

class UNiagaraShaderStageBase;

UCLASS()
/** Container for one or more NiagaraStackEventScriptItemGroups, allowing multiple event handlers per script.*/
class NIAGARAEDITOR_API UNiagaraStackShaderStagesGroup : public UNiagaraStackItemGroup
{
	GENERATED_BODY()

public:
	DECLARE_DELEGATE(FOnItemAdded);

public:
	void Initialize(FRequiredEntryData InRequiredEntryData);

	void SetOnItemAdded(FOnItemAdded InOnItemAdded);

private:
	void ItemAddedFromUtilties(UNiagaraShaderStageBase* AddedShaderStage);

private:
	TSharedPtr<INiagaraStackItemGroupAddUtilities> AddUtilities;
	FOnItemAdded ItemAddedDelegate;
};
