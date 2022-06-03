// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Widgets/SMVVMViewModelBindingListWidget.h"
#include "Widgets/SCompoundWidget.h"

class FWidgetBlueprintEditor;
class SSearchBox;
class STableViewBase;
template<typename ItemType>
class SListView;
class UBlueprintExtension;
class UMVVMBlueprintView;

namespace UE::MVVM
{

/**
 * 
 */
class SMVVMViewModelPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMVVMViewModelPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<FWidgetBlueprintEditor> Editor);
	virtual ~SMVVMViewModelPanel();

private:
	void HandleViewUpdated(UBlueprintExtension*);
	void HandleViewModelsUpdated();
	void HandleSearchChanged(const FText& InFilterText);
	bool HandleViewModelRenamed(const SViewModelBindingListWidget::FViewModel& ViewModel, const FText& RenameTo, bool bCommit, FText& OutErrorMessage);
	void HandleBlueprintCompiled();

	void GenerateViewModelTreeView();

private:
	TSharedPtr<SSearchBox> SearchBoxPtr;
	TSharedPtr<SViewModelBindingListWidget> ViewModelTreeView;

	TWeakPtr<FWidgetBlueprintEditor> WeakBlueprintEditor;
	TWeakObjectPtr<UMVVMBlueprintView> WeakBlueprintView;
};

} // namespace
