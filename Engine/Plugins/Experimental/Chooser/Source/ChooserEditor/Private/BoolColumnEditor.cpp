// Copyright Epic Games, Inc. All Rights Reserved.

#include "BoolColumnEditor.h"
#include "BoolColumn.h"
#include "ContextPropertyWidget.h"
#include "ObjectChooserWidgetFactories.h"
#include "ChooserTableEditor.h"
#include "Widgets/Input/SCheckBox.h"
#include "GraphEditorSettings.h"

#define LOCTEXT_NAMESPACE "BoolColumnEditor"

namespace UE::ChooserEditor
{

TSharedRef<SWidget> CreateBoolColumnWidget(UObject* Column, int Row)
{
	UChooserColumnBool* BoolColumn = Cast<UChooserColumnBool>(Column);

	return SNew (SCheckBox)
	.OnCheckStateChanged_Lambda([BoolColumn,Row](ECheckBoxState State)
	{
		if (Row < BoolColumn->RowValues.Num())
		{
			const FScopedTransaction Transaction(LOCTEXT("Change Bool Value", "Change Bool Value"));
			BoolColumn->Modify(true);
			BoolColumn->RowValues[Row] = (State == ECheckBoxState::Checked);
		}
	})
	.IsChecked_Lambda([BoolColumn, Row]()
	{
		const bool value = (Row < BoolColumn->RowValues.Num()) ? BoolColumn->RowValues[Row] : false;
		return value ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	});
}
	
TSharedRef<SWidget> CreateBoolPropertyWidget(UObject* Object, UClass* ContextClass)
{
	return CreatePropertyWidget<UChooserParameterBool_ContextProperty>(Object, ContextClass, GetDefault<UGraphEditorSettings>()->BooleanPinTypeColor);
}
	
void RegisterBoolWidgets()
{
	FObjectChooserWidgetFactories::ChooserWidgetCreators.Add(UChooserParameterBool_ContextProperty::StaticClass(), CreateBoolPropertyWidget);
	FObjectChooserWidgetFactories::ChooserTextConverter.Add(UChooserParameterBool_ContextProperty::StaticClass(), ConvertToText_ContextProperty<UChooserParameterBool_ContextProperty>);

	FChooserTableEditor::ColumnWidgetCreators.Add(UChooserColumnBool::StaticClass(), CreateBoolColumnWidget);
}
	
}

#undef LOCTEXT_NAMESPACE
