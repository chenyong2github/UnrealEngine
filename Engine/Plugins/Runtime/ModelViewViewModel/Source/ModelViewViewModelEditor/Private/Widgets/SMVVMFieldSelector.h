// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MVVMFieldVariantListTraits.h"
#include "MVVMPropertyPathHelpers.h" 
#include "Styling/CoreStyle.h"
#include "Templates/ValueOrError.h"
#include "Types/MVVMBindingMode.h"
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
DECLARE_DELEGATE_RetVal_OneParam(FIsFieldValidResult, FIsFieldValid, UE::MVVM::FMVVMConstFieldVariant);

class SMVVMFieldEntry : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SMVVMFieldEntry) :
		_TextStyle( &FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>( "NormalText" ) )
		{}
		SLATE_STYLE_ARGUMENT(FTextBlockStyle, TextStyle)
		SLATE_ARGUMENT(UE::MVVM::FMVVMConstFieldVariant, Field)
		SLATE_EVENT(FIsFieldValid, OnValidateField)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void Refresh(); 
	void SetField(const UE::MVVM::FMVVMConstFieldVariant& InField);

private:
	UE::MVVM::FMVVMConstFieldVariant Field;
	FIsFieldValid OnValidateField;
	TSharedPtr<SMVVMFieldIcon> Icon;
	TSharedPtr<STextBlock> Label;
};

class SMVVMFieldSelector : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FSelectionChanged, UE::MVVM::FMVVMConstFieldVariant);

	SLATE_BEGIN_ARGS(SMVVMFieldSelector) :
		_TextStyle(&FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText")),
		_IsSource(false)
		{
		}
		SLATE_STYLE_ARGUMENT(FTextBlockStyle, TextStyle)
		SLATE_ARGUMENT(TArray<UE::MVVM::IFieldPathHelper*>, PathHelpers)
		SLATE_ARGUMENT(TArray<UE::MVVM::IFieldPathHelper*>, CounterpartHelpers)
		SLATE_ATTRIBUTE(EMVVMBindingMode, BindingMode)
		SLATE_ARGUMENT(bool, IsSource)
		SLATE_EVENT(FSelectionChanged, OnSelectionChanged)
		SLATE_EVENT(FIsFieldValid, OnValidateField)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void Refresh();

private:
	TSharedRef<SWidget> OnGenerateFieldWidget(UE::MVVM::FMVVMConstFieldVariant FieldPath) const;
	void OnComboBoxSelectionChanged(UE::MVVM::FMVVMConstFieldVariant Selected, ESelectInfo::Type SelectionType);
	TOptional<UE::MVVM::FBindingSource> GetSelectedSource() const;

	TValueOrError<bool, FString> IsValidBindingForField(const UE::MVVM::FMVVMConstFieldVariant& Field, const UE::MVVM::FMVVMConstFieldVariant& CounterpartField) const;
	TValueOrError<bool, FString> ValidateField(UE::MVVM::FMVVMConstFieldVariant Field) const;

	EVisibility GetClearVisibility() const;
	FReply OnClearBinding();

private:
	TArray<UE::MVVM::IFieldPathHelper*> PathHelpers;
	TArray<UE::MVVM::IFieldPathHelper*> CounterpartHelpers;
	TAttribute<EMVVMBindingMode> BindingMode;
	TSharedPtr<SMVVMFieldEntry> SelectedEntry;
	TOptional<UE::MVVM::FBindingSource> SelectedSource;
	const FTextBlockStyle* TextStyle = nullptr;
	bool bIsSource = false;

	TSharedPtr<SComboBox<UE::MVVM::FMVVMConstFieldVariant>> FieldComboBox;

	FSelectionChanged OnSelectionChangedDelegate;
	FIsFieldValid OnValidateFieldDelegate;

	TArray<UE::MVVM::FMVVMConstFieldVariant> AvailableFields;
}; 
