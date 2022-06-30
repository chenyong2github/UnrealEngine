// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Widgets/PropertyViewer/SPropertyViewer.h"
#include "Widgets/SMVVMViewModelBindingListWidget.h"
#include "Widgets/SCompoundWidget.h"

class FWidgetBlueprintEditor;
class SPositiveActionButton;
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
	void HandleViewUpdated(UBlueprintExtension* Extension);
	void HandleViewModelsUpdated();
	//bool HandleViewModelRenamed(const SViewModelBindingListWidget::FViewModel& ViewModel, const FText& RenameTo, bool bCommit, FText& OutErrorMessage);
	TSharedRef<SWidget> MakeAddMenu();
	void HandleCancelAddMenu();
	void HandleAddMenuViewModel(const UClass* SelectedClass);
	bool HandleCanEditViewmodelList() const;

	void FillViewModel();

private:
	TSharedPtr<SPositiveActionButton> AddMenuButton;
	TSharedPtr<UE::PropertyViewer::SPropertyViewer> ViewModelTreeView;

	using FViewModelHandle = TPair<FGuid, UE::PropertyViewer::SPropertyViewer::FHandle>;
	TArray<FViewModelHandle> ViewModelHandles;

	TWeakPtr<FWidgetBlueprintEditor> WeakBlueprintEditor;
	TWeakObjectPtr<UMVVMBlueprintView> WeakBlueprintView;
	TUniquePtr<FFieldIterator_ViewModel> ViewModelFieldIterator;
	FDelegateHandle ViewModelsUpdatedHandle;
};

} // namespace
