// Copyright Epic Games, Inc. All Rights Reserved.

#include "FloatRangeColumnEditor.h"
#include "FloatRangeColumn.h"
#include "ObjectChooserWidgetFactories.h"
#include "ChooserTableEditor.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "GraphEditorSettings.h"
#include "SPropertyAccessChainWidget.h"

#define LOCTEXT_NAMESPACE "FloatRangeColumnEditor"

namespace UE::ChooserEditor
{
TSharedRef<SWidget> CreateFloatPropertyWidget(bool bReadOnly, UObject* TransactionObject, void* Value, UClass* ContextClass, UClass* ResultBaseClass)
{
	IHasContextClass* HasContextClass = Cast<IHasContextClass>(TransactionObject);

	FFloatContextProperty* ContextProperty = reinterpret_cast<FFloatContextProperty*>(Value);

	return SNew(SPropertyAccessChainWidget).ContextClassOwner(HasContextClass).AllowFunctions(true).BindingColor("FloatPinTypeColor").TypeFilter("double")
	.PropertyBindingValue(&ContextProperty->Binding)
	.OnAddBinding_Lambda(
		[ContextProperty, TransactionObject](FName InPropertyName, const TArray<FBindingChainElement>& InBindingChain)
		{
			const FScopedTransaction Transaction(NSLOCTEXT("ContextPropertyWidget", "Change Property Binding", "Change Property Binding"));
			TransactionObject->Modify(true);
			ContextProperty->SetBinding(InBindingChain);	
		});
}
	
TSharedRef<SWidget> CreateFloatRangeColumnWidget(UChooserTable* Chooser, FChooserColumnBase* Column, int Row)
{
	FFloatRangeColumn* FloatRangeColumn = static_cast<FFloatRangeColumn*>(Column);

	return SNew(SHorizontalBox)
	+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
	[
		SNew(STextBlock).Text(LOCTEXT("FloatRangeLeft", "("))
	]
	+ SHorizontalBox::Slot().FillWidth(0.5f)
	[
		SNew(SNumericEntryBox<float>)
		.MaxValue_Lambda([FloatRangeColumn, Row]()
		{
			return (Row < FloatRangeColumn->RowValues.Num()) ? FloatRangeColumn->RowValues[Row].Max : 0;
		})
		.Value_Lambda([FloatRangeColumn, Row]()
		{
			return (Row < FloatRangeColumn->RowValues.Num()) ? FloatRangeColumn->RowValues[Row].Min : 0;
		})
		.OnValueCommitted_Lambda([Chooser, FloatRangeColumn, Row](float NewValue, ETextCommit::Type CommitType)
		{
			if (Row < FloatRangeColumn->RowValues.Num())
			{
				const FScopedTransaction Transaction(LOCTEXT("Edit Min Value", "Edit Min Value"));
				Chooser->Modify(true);
				FloatRangeColumn->RowValues[Row].Min = NewValue;
			}
		})
	]
	+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
	[
		SNew(STextBlock).Text(LOCTEXT("FloatRangeComma", " ,"))
	]
	+ SHorizontalBox::Slot().FillWidth(0.5f)
	[
		SNew(SNumericEntryBox<float>)
		.MinValue_Lambda([FloatRangeColumn, Row]()
		{
			return (Row < FloatRangeColumn->RowValues.Num()) ? FloatRangeColumn->RowValues[Row].Min : 0;
		})
		.Value_Lambda([FloatRangeColumn, Row]()
		{
			return (Row < FloatRangeColumn->RowValues.Num()) ? FloatRangeColumn->RowValues[Row].Max : 0;
		})
		.OnValueCommitted_Lambda([Chooser, FloatRangeColumn, Row](float NewValue, ETextCommit::Type CommitType)
		{
			if (Row < FloatRangeColumn->RowValues.Num())
			{
				const FScopedTransaction Transaction(LOCTEXT("Edit Max", "Edit Max Value"));
				Chooser->Modify(true);
				FloatRangeColumn->RowValues[Row].Max = NewValue;
			}
		})
	]
	+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
	[
		SNew(STextBlock).Text(LOCTEXT("FloatRangeRight", " )"))
	];
}

	
void RegisterFloatRangeWidgets()
{
	FObjectChooserWidgetFactories::RegisterWidgetCreator(FFloatContextProperty::StaticStruct(), CreateFloatPropertyWidget);
	FObjectChooserWidgetFactories::RegisterColumnWidgetCreator(FFloatRangeColumn::StaticStruct(), CreateFloatRangeColumnWidget);
}
	
}

#undef LOCTEXT_NAMESPACE
