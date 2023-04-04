// Copyright Epic Games, Inc. All Rights Reserved.

#include "RandomizeColumnEditor.h"
#include "RandomizeColumn.h"
#include "SPropertyAccessChainWidget.h"
#include "ObjectChooserWidgetFactories.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "GraphEditorSettings.h"

#define LOCTEXT_NAMESPACE "RandomizeColumnEditor"

namespace UE::ChooserEditor
{

TSharedRef<SWidget> CreateRandomizeColumnWidget(UChooserTable* Chooser, FChooserColumnBase* Column, int Row)
{
	FRandomizeColumn* RandomizeColumn = static_cast<FRandomizeColumn*>(Column);
	
	return
	SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(75).Content()
			[
				SNew(SNumericEntryBox<float>)
    				.Value_Lambda([RandomizeColumn, Row]()
    				{
    					if (!RandomizeColumn->RowValues.IsValidIndex(Row))
    					{
    						return 0.0f;
    					}
    					return RandomizeColumn->RowValues[Row];
    				})
    				.OnValueCommitted_Lambda([Chooser, Row, RandomizeColumn](float Value, ETextCommit::Type CommitType)
    				{
    					if (RandomizeColumn->RowValues.IsValidIndex(Row))
    					{
    						const FScopedTransaction Transaction(LOCTEXT("Edit Randomize Cell Data", "Edit Randomize Cell Data"));
    						Chooser->Modify(true);
    						RandomizeColumn->RowValues[Row] = Value;
    					}
    				})
    		]
    	]
		+ SHorizontalBox::Slot().FillWidth(1);
}
	
TSharedRef<SWidget> CreateRandomizePropertyWidget(bool bReadOnly, UObject* TransactionObject, void* Value, UClass* ContextClass, UClass* ResultBaseClass)
{
	IHasContextClass* HasContextClass = Cast<IHasContextClass>(TransactionObject);

	FRandomizeContextProperty* ContextProperty = reinterpret_cast<FRandomizeContextProperty*>(Value);

	return SNew(SPropertyAccessChainWidget).ContextClassOwner(HasContextClass).AllowFunctions(true).BindingColor("StructPinTypeColor").TypeFilter("FChooserRandomizationContext")
	.PropertyBindingValue(&ContextProperty->Binding)
	.OnAddBinding_Lambda(
		[ContextProperty, TransactionObject](FName InPropertyName, const TArray<FBindingChainElement>& InBindingChain)
		{
			const FScopedTransaction Transaction(NSLOCTEXT("ContextPropertyWidget", "Change Property Binding", "Change Property Binding"));
			TransactionObject->Modify(true);
			ContextProperty->SetBinding(InBindingChain);	
		});
}
	
void RegisterRandomizeWidgets()
{
	FObjectChooserWidgetFactories::RegisterWidgetCreator(FRandomizeContextProperty::StaticStruct(), CreateRandomizePropertyWidget);
	FObjectChooserWidgetFactories::RegisterColumnWidgetCreator(FRandomizeColumn::StaticStruct(), CreateRandomizeColumnWidget);
}
	
}

#undef LOCTEXT_NAMESPACE
