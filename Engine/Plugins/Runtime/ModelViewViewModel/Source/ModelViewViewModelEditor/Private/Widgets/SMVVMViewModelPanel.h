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
		TSharedRef<SWidget> MakeAddMenu();
		void HandleCancelAddMenu();
		void HandleAddMenuViewModel(const UClass* SelectedClass);
		bool HandleCanEditViewmodelList() const;

		void FillViewModel();

	private:
		TSharedPtr<SPositiveActionButton> AddMenuButton;
		TSharedPtr<UE::PropertyViewer::SPropertyViewer> ViewModelTreeView;
		TUniquePtr<FFieldIterator_Bindable> FieldIterator;

		TWeakPtr<FWidgetBlueprintEditor> WeakBlueprintEditor;
		TWeakObjectPtr<UMVVMBlueprintView> WeakBlueprintView;
		FDelegateHandle ViewModelsUpdatedHandle;
	};

} // namespace
