// Copyright Epic Games, Inc. All Rights Reserved.

#include "BoolColumnEditor.h"
#include "BoolColumn.h"
#include "OutputBoolColumn.h"
#include "ContextPropertyWidget.h"
#include "ChooserTableEditor.h"
#include "ObjectChooserWidgetFactories.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "GraphEditorSettings.h"

#define LOCTEXT_NAMESPACE "BoolColumnEditor"

namespace UE::ChooserEditor
{

TSharedRef<SWidget> CreateBoolColumnWidget(UChooserTable* Chooser, FChooserColumnBase* Column, int Row)
{
	FBoolColumn* BoolColumn = static_cast<FBoolColumn*>(Column);
	
	return
	SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1)
		+ SHorizontalBox::Slot().MaxWidth(100).HAlign(HAlign_Center)
		[
			SNew(SButton).ButtonStyle(FAppStyle::Get(),"FlatButton").HAlign(HAlign_Center)
    				.Text_Lambda([Row, BoolColumn]()
    				{
    					if (!BoolColumn->RowValuesWithAny.IsValidIndex(Row))
    					{
    						return FText();
    					}
    					switch (BoolColumn->RowValuesWithAny[Row])
    					{
							case EBoolColumnCellValue::MatchAny: return LOCTEXT("Any","Any");
							case EBoolColumnCellValue::MatchTrue: return LOCTEXT("True","True");
							case EBoolColumnCellValue::MatchFalse: return LOCTEXT("False","False");
    					}
    					return FText();
    				})
    				.OnClicked_Lambda([Chooser, Row, BoolColumn]()
    				{
    					if (BoolColumn->RowValuesWithAny.IsValidIndex(Row))
    					{
    						const FScopedTransaction Transaction(LOCTEXT("Edit Bool Cell Data", "Edit Bool Cell Data"));
    						Chooser->Modify(true);
    						BoolColumn->RowValuesWithAny[Row] = static_cast<EBoolColumnCellValue>((static_cast<int>(BoolColumn->RowValuesWithAny[Row]) + 1) % 3);
    					}
    					return FReply::Handled();
    				})
    	]
		+ SHorizontalBox::Slot().FillWidth(1);
}
	
TSharedRef<SWidget> CreateOutputBoolColumnWidget(UChooserTable* Chooser, FChooserColumnBase* Column, int Row)
{
	FOutputBoolColumn* BoolColumn = static_cast<FOutputBoolColumn*>(Column);

	return SNew (SCheckBox)
	.OnCheckStateChanged_Lambda([Chooser, BoolColumn,Row](ECheckBoxState State)
	{
		if (Row < BoolColumn->RowValues.Num())
		{
			const FScopedTransaction Transaction(LOCTEXT("Change Bool Value", "Change Bool Value"));
			Chooser->Modify(true);
			BoolColumn->RowValues[Row] = (State == ECheckBoxState::Checked);
		}
	})
	.IsChecked_Lambda([BoolColumn, Row]()
	{
		const bool value = (Row < BoolColumn->RowValues.Num()) ? BoolColumn->RowValues[Row] : false;
		return value ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	});
}
	
TSharedRef<SWidget> CreateBoolPropertyWidget(bool bReadOnly, UObject* TransactionObject, void* Value, UClass* ContextClass, UClass* ResultBaseClass)
{
	return CreatePropertyWidget<FBoolContextProperty>(bReadOnly, TransactionObject, Value, ContextClass, GetDefault<UGraphEditorSettings>()->BooleanPinTypeColor);
}
	
void RegisterBoolWidgets()
{
	FObjectChooserWidgetFactories::RegisterWidgetCreator(FBoolContextProperty::StaticStruct(), CreateBoolPropertyWidget);
	FObjectChooserWidgetFactories::RegisterColumnWidgetCreator(FBoolColumn::StaticStruct(), CreateBoolColumnWidget);
	FObjectChooserWidgetFactories::RegisterColumnWidgetCreator(FOutputBoolColumn::StaticStruct(), CreateOutputBoolColumnWidget);
}
	
}

#undef LOCTEXT_NAMESPACE
