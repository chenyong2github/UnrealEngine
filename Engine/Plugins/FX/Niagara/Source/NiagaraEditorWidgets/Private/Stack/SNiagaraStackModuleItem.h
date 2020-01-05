// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SNiagaraStackEntryWidget.h"
#include "Styling/SlateTypes.h"
#include "Layout/Visibility.h"

class UNiagaraStackModuleItem;
class UNiagaraStackViewModel;

class SNiagaraStackModuleItem : public SNiagaraStackEntryWidget
{
public:
	SLATE_BEGIN_ARGS(SNiagaraStackModuleItem) { }
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, UNiagaraStackModuleItem& InModuleItem, UNiagaraStackViewModel* InStackViewModel);

	void SetEnabled(bool bInIsEnabled);

	bool CheckEnabledStatus(bool bIsEnabled);

	void FillRowContextMenu(class FMenuBuilder& MenuBuilder);

	//~ SWidget interface
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	ECheckBoxState GetCheckState() const;

	void OnCheckStateChanged(ECheckBoxState InCheckState);

	bool GetButtonsEnabled() const;

	FText GetDeleteButtonToolTipText() const;

	bool GetDeleteButtonEnabled() const;

	bool GetEnabledCheckBoxEnabled() const;

	EVisibility GetRaiseActionMenuVisibility() const;

	EVisibility GetRefreshVisibility() const;

	FReply DeleteClicked();
	
	TSharedRef<SWidget> RaiseActionMenuClicked();

	bool CanRaiseActionMenu() const;

	FReply RefreshClicked();

	FReply OnModuleItemDrop(TSharedPtr<class FDragDropOperation> DragDropOperation);

	bool OnModuleItemAllowDrop(TSharedPtr<class FDragDropOperation> DragDropOperation);

	void ShowReassignModuleScriptMenu();

private:
	UNiagaraStackModuleItem* ModuleItem;
};