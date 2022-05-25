// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dataflow/DataflowProperty.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailCustomization.h"
#include "Misc/Optional.h"
#include "Widgets/Input/SNumericEntryBox.h"

namespace Dataflow
{
	// todo(Dataflow) : Add support for FFilename as SFilePathPicker

	//
	//  T
	//
	template<class T> DATAFLOWEDITOR_API void PropertyWidgetFactory(
		IDetailLayoutBuilder& InDetailBuilder,
		TSharedPtr<IPropertyHandle> PropertyHandle,
		TSharedPtr<Dataflow::FNode> InNode,
		TProperty<T>* InProperty) 
	{
		InDetailBuilder.AddCustomRowToCategory(PropertyHandle, FText::FromString("PropertyHandel<T>"))
			.NameContent()[
				SNew(STextBlock).Font(IDetailLayoutBuilder::GetDetailFont())
					.Text(FText::FromName(InProperty->GetName()))
			].ValueContent()[
				SNew(SNumericEntryBox<T>)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.MinValue(TOptional<T>())
					.MaxValue(TOptional<T>())
					.MinSliderValue(TOptional<T>()) // No lower limit
					.MaxSliderValue(TOptional<T>()) // No upper limit
					.Value_Lambda([InNode, InProperty]()
						{
							return InProperty->GetValue();
						})
					.OnValueCommitted_Lambda([InNode, InProperty](T NewValue, ETextCommit::Type CommitType)
						{
							InProperty->SetValue(NewValue);
						})
			];

	}

	//
	// bool
	//
	template<> inline
		void PropertyWidgetFactory<bool>(
			IDetailLayoutBuilder& InDetailBuilder,
			TSharedPtr<IPropertyHandle> PropertyHandle,
			TSharedPtr<Dataflow::FNode> InNode,
			TProperty<bool>* InProperty)
	{
		InDetailBuilder.AddCustomRowToCategory(PropertyHandle, FText::FromString("PropertyHandel<bool>"))
			.NameContent()[
				SNew(STextBlock).Font(IDetailLayoutBuilder::GetDetailFont())
					.Text(FText::FromName(InProperty->GetName()))
			].ValueContent()[
				SNew(SCheckBox)
					.IsChecked_Lambda([InNode, InProperty]() -> ECheckBoxState
						{
							return InProperty->GetValue()?ECheckBoxState::Checked:ECheckBoxState::Unchecked;
						})
					.OnCheckStateChanged_Lambda([InNode, InProperty](ECheckBoxState NewState)
						{
							InProperty->SetValue(NewState == ECheckBoxState::Checked);
						})

			];
	}

	//
	// FString
	//
	template<> inline
	void PropertyWidgetFactory<FString>(
		IDetailLayoutBuilder& InDetailBuilder,
		TSharedPtr<IPropertyHandle> PropertyHandle,
		TSharedPtr<Dataflow::FNode> InNode,
		TProperty<FString>* InProperty)
	{
		InDetailBuilder.AddCustomRowToCategory(PropertyHandle, FText::FromString("PropertyHandel<FString>"))
			.NameContent()[
				SNew(STextBlock).Font(IDetailLayoutBuilder::GetDetailFont())
					.Text(FText::FromName(InProperty->GetName()))
			].ValueContent()[
				SNew(SEditableTextBox)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text_Lambda([InNode, InProperty]()
						{
							return FText::FromString(InProperty->GetValue());
						})
					.OnTextCommitted_Lambda([InNode, InProperty](const FText& InText, ETextCommit::Type TextCommitType)
						{
							InProperty->SetValue(InText.ToString());
						})

			];
	}

}
