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
	DECLARE_DELEGATE_OneParam(FOnSelectionChanged, const FMVVMBlueprintPropertyPath&);

	SLATE_BEGIN_ARGS(SFieldSelector) :
		_TextStyle(&FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
		{
		}
		SLATE_STYLE_ARGUMENT(FTextBlockStyle, TextStyle)
		SLATE_ATTRIBUTE(FMVVMBlueprintPropertyPath, SelectedField)
		SLATE_EVENT(FOnSelectionChanged, OnSelectionChanged)
		
		/**
		 * Get the binding mode for this field. 
		 * Required for validating the availability of functions, eg. SetFoo() can only be used as a setter.
		 */
		SLATE_ATTRIBUTE(EMVVMBindingMode, BindingMode)

		/** 
		 * Should we show the source as well as the field?
		 * If this is set to false, then the Source attribute must be set.
		 */
		SLATE_ARGUMENT_DEFAULT(bool, ShowSource) = true;

		/**
		 * Only show properties assignable to the given property.
		 */
		SLATE_ARGUMENT_DEFAULT(const FProperty*, AssignableTo) = nullptr;

		/** The source to use. Only used if ShowSource is false. */
		SLATE_ARGUMENT(FBindingSource, Source)
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

	void OnWidgetSelected(FName WidgetName, ESelectInfo::Type);

	void OnSearchTextChanged(const FText& NewText);

private:
	TAttribute<FMVVMBlueprintPropertyPath> SelectedField;
	TAttribute<EMVVMBindingMode> BindingMode;

	TSharedPtr<SFieldEntry> SelectedEntryWidget;
	TSharedPtr<SSourceEntry> SelectedSourceWidget;

	const FTextBlockStyle* TextStyle = nullptr;

	FOnSelectionChanged OnSelectionChangedDelegate;

	FMVVMBlueprintPropertyPath CachedSelectedField;

	TSharedPtr<SSourceBindingList> BindingList;
	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<SComboButton> ComboButton;

	TOptional<FBindingSource> FixedSource;

	const UWidgetBlueprint* WidgetBlueprint = nullptr;
	bool bViewModelProperty = false;

	TArray<FBindingSource> ViewModelSources;
	TSharedPtr<SListView<FBindingSource>> ViewModelList;
	TSharedPtr<SReadOnlyHierarchyView> WidgetList;

	const FProperty* AssignableTo = nullptr;
}; 

} // namespace UE::MVVM