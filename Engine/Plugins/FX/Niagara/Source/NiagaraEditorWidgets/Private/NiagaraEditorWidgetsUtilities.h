// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/NameTypes.h"

class UNiagaraStackEntry;
class UNiagaraStackItem;
class UNiagaraStackModuleItem;

namespace FNiagaraStackEditorWidgetsUtilities
{
	FName GetColorNameForExecutionCategory(FName ExecutionCategoryName);

	FName GetIconNameForExecutionSubcategory(FName ExecutionSubcategoryName, bool bIsHighlighted);

	FName GetIconColorNameForExecutionCategory(FName ExecutionCategoryName);
	
	bool AddStackEntryAssetContextMenuActions(FMenuBuilder& MenuBuilder, UNiagaraStackEntry& StackEntry);

	bool AddStackItemContextMenuActions(FMenuBuilder& MenuBuilder, UNiagaraStackItem& StackItem);

	bool AddStackModuleItemContextMenuActions(FMenuBuilder& MenuBuilder, UNiagaraStackModuleItem& StackItem, TSharedRef<SWidget> TargetWidget);

}