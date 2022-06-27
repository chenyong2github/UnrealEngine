// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MVVMFieldVariantListTraits.h"
#include "Styling/CoreStyle.h"
#include "Templates/ValueOrError.h"
#include "Types/MVVMBindingMode.h"
#include "Types/MVVMBindingSource.h"
#include "Types/MVVMFieldVariant.h"
#include "UObject/UnrealType.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"

namespace UE::MVVM
{
	class IFieldPathHelper;
}

class SMVVMFieldIcon;
class STextBlock; 

using FIsFieldValidResult = TValueOrError<bool, FString>;
DECLARE_DELEGATE_RetVal_OneParam(FIsFieldValidResult, FIsFieldValid, FMVVMBlueprintPropertyPath);

class SMVVMFieldEntry : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SMVVMFieldEntry) :
		_TextStyle( &FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>( "NormalText" ) )
		{}
		SLATE_STYLE_ARGUMENT(FTextBlockStyle, TextStyle)
		SLATE_ARGUMENT(FMVVMBlueprintPropertyPath, Field)
		SLATE_EVENT(FIsFieldValid, OnValidateField)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void Refresh(); 
	void SetField(const FMVVMBlueprintPropertyPath& InField);

private:
	FMVVMBlueprintPropertyPath Field;
	FIsFieldValid OnValidateField;
	TSharedPtr<SMVVMFieldIcon> Icon;
	TSharedPtr<STextBlock> Label;
};

class SMVVMFieldSelector : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FSelectionChanged, FMVVMBlueprintPropertyPath);

	SLATE_BEGIN_ARGS(SMVVMFieldSelector) :
		_TextStyle(&FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText")),
		_IsSource(false)
		{
		}
		SLATE_STYLE_ARGUMENT(FTextBlockStyle, TextStyle)
		SLATE_ATTRIBUTE(EMVVMBindingMode, BindingMode)
		SLATE_ARGUMENT(bool, IsSource)
		SLATE_ATTRIBUTE(UE::MVVM::FBindingSource, SelectedSource)
		SLATE_ATTRIBUTE(FMVVMBlueprintPropertyPath, SelectedField)
		SLATE_ATTRIBUTE(TArray<FMVVMBlueprintPropertyPath>, AvailableFields)
		SLATE_EVENT(FSelectionChanged, OnSelectionChanged)

		/**
		  * Would the given field be a valid entry for this combo box? 
		  * The error string returned will be used as a tooltip.
		  */
		SLATE_EVENT(FIsFieldValid, OnValidateField)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void Refresh();

private:
	TSharedRef<SWidget> OnGenerateFieldWidget(FMVVMBlueprintPropertyPath Path) const;
	void OnComboBoxSelectionChanged(FMVVMBlueprintPropertyPath Selected, ESelectInfo::Type SelectionType);

	TValueOrError<bool, FString> ValidateField(FMVVMBlueprintPropertyPath Field) const;

	EVisibility GetClearVisibility() const;
	FReply OnClearBinding();

private:
	TAttribute<EMVVMBindingMode> BindingMode;
	TSharedPtr<SMVVMFieldEntry> SelectedEntryWidget;
	TAttribute<UE::MVVM::FBindingSource> SelectedSource;
	TAttribute<FMVVMBlueprintPropertyPath> SelectedField;
	TAttribute<TArray<FMVVMBlueprintPropertyPath>> AvailableFields;

	const FTextBlockStyle* TextStyle = nullptr;
	bool bIsSource = false;

	TSharedPtr<SComboBox<FMVVMBlueprintPropertyPath>> FieldComboBox;

	FSelectionChanged OnSelectionChangedDelegate;
	FIsFieldValid OnValidateFieldDelegate;

	TArray<FMVVMBlueprintPropertyPath> CachedAvailableFields;
}; 
