// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SNiagaraStackItem.h"
#include "Styling/SlateTypes.h"
#include "Layout/Visibility.h"
#include "Types/SlateEnums.h"

class UNiagaraStackModuleItem;
class UNiagaraStackViewModel;
class SNiagaraStackDisplayName;
struct FGraphActionListBuilderBase;
class SComboButton;

class SNiagaraStackModuleItem : public SNiagaraStackItem
{
public:
	SLATE_BEGIN_ARGS(SNiagaraStackModuleItem) { }
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, UNiagaraStackModuleItem& InModuleItem, UNiagaraStackViewModel* InStackViewModel);

	void FillRowContextMenu(class FMenuBuilder& MenuBuilder);

	//~ SWidget interface
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

protected:
	virtual void AddCustomRowWidgets(TSharedRef<SHorizontalBox> HorizontalBox) override;

	virtual TSharedRef<SWidget> AddContainerForRowWidgets(TSharedRef<SWidget> RowWidgets) override;

private:
	bool GetButtonsEnabled() const;

	EVisibility GetRaiseActionMenuVisibility() const;

	EVisibility GetRefreshVisibility() const;

	FReply ScratchButtonPressed() const;
	
	TSharedRef<SWidget> RaiseActionMenuClicked();

	bool CanRaiseActionMenu() const;

	FReply RefreshClicked();

	FReply OnModuleItemDrop(TSharedPtr<class FDragDropOperation> DragDropOperation);

	bool OnModuleItemAllowDrop(TSharedPtr<class FDragDropOperation> DragDropOperation);

	void CollectParameterActions(FGraphActionListBuilderBase& ModuleActions);

	void CollectModuleActions(FGraphActionListBuilderBase& ModuleActions);

	void ShowReassignModuleScriptMenu();

	bool GetLibraryOnly() const;

	void SetLibraryOnly(bool bInLibraryOnly);

private:
	UNiagaraStackModuleItem* ModuleItem;

	TSharedPtr<SComboButton> AddButton;

	static bool bLibraryOnly;
};