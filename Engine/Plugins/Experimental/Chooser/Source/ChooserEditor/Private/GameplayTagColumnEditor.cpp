// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayTagColumnEditor.h"
#include "ContextPropertyWidget.h"
#include "GameplayTagColumn.h"
#include "ObjectChooserWidgetFactories.h"
#include "ChooserTableEditor.h"
#include "SGameplayTagWidget.h"
#include "SSimpleComboButton.h"
#include "GraphEditorSettings.h"

#define LOCTEXT_NAMESPACE "GameplayTagColumnEditor"

namespace UE::ChooserEditor
{

TSharedRef<SWidget> CreateGameplayTagColumnWidget(UObject* Column, int Row)
{
	UChooserColumnGameplayTag* GameplayTagColumn = Cast<UChooserColumnGameplayTag>(Column);

	return SNew(SSimpleComboButton)
		.Text_Lambda([GameplayTagColumn, Row]()
		{
			if (Row < GameplayTagColumn->RowValues.Num())
			{
				FText Result = FText::FromString(GameplayTagColumn->RowValues[Row].ToStringSimple(false));
				if (Result.IsEmpty())
				{
					Result = LOCTEXT("Any Tag", "[Any]");
				}
				return Result;
			}
			else
			{
				return FText();
			}
		})	
		.OnGetMenuContent_Lambda([GameplayTagColumn, Row]()
		{
			if (Row < GameplayTagColumn->RowValues.Num())
			{
				TArray<SGameplayTagWidget::FEditableGameplayTagContainerDatum> EditableContainers;
				EditableContainers.Emplace(GameplayTagColumn, &(GameplayTagColumn->RowValues[Row]));
				return TSharedRef<SWidget>(SNew(SGameplayTagWidget, EditableContainers));
			}

			return SNullWidget::NullWidget;
		}
	);
}

TSharedRef<SWidget> CreateGameplayTagPropertyWidget(UObject* Object, UClass* ContextClass)
{
	return CreatePropertyWidget<UChooserParameterGameplayTag_ContextProperty>(Object, ContextClass, GetDefault<UGraphEditorSettings>()->StructPinTypeColor);
}
	
void RegisterGameplayTagWidgets()
{
	FObjectChooserWidgetFactories::ChooserWidgetCreators.Add(UChooserParameterGameplayTag_ContextProperty::StaticClass(), CreateGameplayTagPropertyWidget);
	FObjectChooserWidgetFactories::ChooserTextConverter.Add(UChooserParameterGameplayTag_ContextProperty::StaticClass(), ConvertToText_ContextProperty<UChooserParameterGameplayTag_ContextProperty>);

	FChooserTableEditor::ColumnWidgetCreators.Add(UChooserColumnGameplayTag::StaticClass(), CreateGameplayTagColumnWidget);
}
	
}

#undef LOCTEXT_NAMESPACE
