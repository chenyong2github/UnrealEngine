// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "UObject/NameTypes.h"
#include "ViewModels/Stack/NiagaraStackEntry.h"

class FMenuBuilder;
class SWidget;
class UNiagaraStackEntry;
class UNiagaraStackItem;
class UNiagaraStackModuleItem;

namespace FNiagaraStackEditorWidgetsUtilities
{
	FName GetColorNameForExecutionCategory(FName ExecutionCategoryName);

	FName GetIconNameForExecutionSubcategory(FName ExecutionSubcategoryName, bool bIsHighlighted);

	FName GetIconColorNameForExecutionCategory(FName ExecutionCategoryName);
	
	bool AddStackEntryAssetContextMenuActions(FMenuBuilder& MenuBuilder, UNiagaraStackEntry& StackEntry);

	bool AddStackEntryContextMenuActions(FMenuBuilder& MenuBuilder, UNiagaraStackEntry& StackEntry);

	bool AddStackItemContextMenuActions(FMenuBuilder& MenuBuilder, UNiagaraStackItem& StackItem);

	bool AddStackModuleItemContextMenuActions(FMenuBuilder& MenuBuilder, UNiagaraStackModuleItem& StackItem, TSharedRef<SWidget> TargetWidget);

	TSharedRef<FDragDropOperation> ConstructDragDropOperationForStackEntries(const TArray<UNiagaraStackEntry*>& DraggedEntries);

	void HandleDragLeave(const FDragDropEvent& InDragDropEvent);

	TOptional<EItemDropZone> RequestDropForStackEntry(const FDragDropEvent& InDragDropEvent, EItemDropZone InDropZone, UNiagaraStackEntry* InTargetEntry, UNiagaraStackEntry::EDropOptions DropOptions);

	bool HandleDropForStackEntry(const FDragDropEvent& InDragDropEvent, EItemDropZone InDropZone, UNiagaraStackEntry* InTargetEntry, UNiagaraStackEntry::EDropOptions DropOptions);

	FString StackEntryToStringForListDebug(UNiagaraStackEntry* StackEntry);
}