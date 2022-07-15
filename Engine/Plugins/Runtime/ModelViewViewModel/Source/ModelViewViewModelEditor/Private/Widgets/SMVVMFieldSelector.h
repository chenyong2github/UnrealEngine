// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MVVMPropertyPath.h"
#include "Styling/CoreStyle.h"
#include "Templates/ValueOrError.h"
#include "Types/MVVMBindingMode.h"
#include "Types/MVVMBindingSource.h"
#include "Types/MVVMFieldVariant.h"
#include "UObject/UnrealType.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SMVVMFieldEntry.h"
#include "Widgets/SMVVMViewModelBindingListWidget.h"
#include "Widgets/Text/STextBlock.h"

class SComboButton;
class SSearchBox;
class SReadOnlyHierarchyView;

namespace UE::MVVM
{

class SSourceEntry;
class SSourceBindingList;

class SFieldSelector : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnSelectionChanged, FMVVMBlueprintPropertyPath);

	SLATE_BEGIN_ARGS(SFieldSelector) :
		_TextStyle(&FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
		{
		}
		SLATE_STYLE_ARGUMENT(FTextBlockStyle, TextStyle)
		SLATE_ATTRIBUTE(FMVVMBlueprintPropertyPath, SelectedField)
		SLATE_EVENT(FOnSelectionChanged, OnSelectionChanged)
		SLATE_ATTRIBUTE(EMVVMBindingMode, BindingMode)

		/**
		  * Would the given field be a valid entry for this combo box? 
		  * The error string returned will be used as a tooltip.
		  */
		SLATE_EVENT(FIsFieldValid, OnValidateField)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const UWidgetBlueprint* InWidgetBlueprint, bool bInViewModelProperty);
	void Refresh();

private:
	TSharedRef<SWidget> OnGenerateFieldWidget(FMVVMBlueprintPropertyPath Path) const;
	TSharedRef<SWidget> OnGetMenuContent();

	TValueOrError<bool, FString> ValidateField(FMVVMBlueprintPropertyPath Field) const;

	bool IsClearEnabled() const;
	FReply OnClearBinding();

	bool IsSelectEnabled() const;
	FReply OnSelectProperty();

	FReply OnCancel();

	void SetSelection(const FMVVMBlueprintPropertyPath& SelectedPath);
	EFieldVisibility GetFieldVisibilityFlags() const;

	void OnViewModelSelected(FBindingSource ViewModel, ESelectInfo::Type);
	TSharedRef<ITableRow> GenerateRowForViewModel(MVVM::FBindingSource ViewModel, const TSharedRef<STableViewBase>& OwnerTable) const;

	void OnSearchTextChanged(const FText& NewText);

private:
	TAttribute<FMVVMBlueprintPropertyPath> SelectedField;
	TAttribute<EMVVMBindingMode> BindingMode;

	TSharedPtr<SFieldEntry> SelectedEntryWidget;
	TSharedPtr<SSourceEntry> SelectedSourceWidget;

	const FTextBlockStyle* TextStyle = nullptr;

	FOnSelectionChanged OnSelectionChangedDelegate;
	FIsFieldValid OnValidateFieldDelegate;

	FMVVMBlueprintPropertyPath CachedSelectedField;

	TSharedPtr<SSourceBindingList> BindingList;
	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<SComboButton> ComboButton;

	const UWidgetBlueprint* WidgetBlueprint = nullptr;
	bool bViewModelProperty = false;

	TArray<FBindingSource> ViewModelSources;
	TSharedPtr<SListView<FBindingSource>> ViewModelList;
}; 

} // namespace UE::MVVM