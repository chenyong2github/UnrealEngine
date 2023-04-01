// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayTagColumnEditor.h"
#include "SPropertyAccessChainWidget.h"
#include "GameplayTagColumn.h"
#include "ObjectChooserWidgetFactories.h"
#include "SGameplayTagWidget.h"
#include "SSimpleComboButton.h"
#include "GraphEditorSettings.h"

#define LOCTEXT_NAMESPACE "FGameplayTagColumnEditor"

namespace UE::ChooserEditor
{

TSharedRef<SWidget> CreateGameplayTagColumnWidget(UChooserTable* Chooser, FChooserColumnBase* Column, int Row)
{
	FGameplayTagColumn* GameplayTagColumn = static_cast<struct FGameplayTagColumn*>(Column);

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
		.OnGetMenuContent_Lambda([Chooser, GameplayTagColumn, Row]()
		{
			if (Row < GameplayTagColumn->RowValues.Num())
			{
				TArray<SGameplayTagWidget::FEditableGameplayTagContainerDatum> EditableContainers;
				EditableContainers.Emplace(Chooser, &(GameplayTagColumn->RowValues[Row]));
				return TSharedRef<SWidget>(SNew(SGameplayTagWidget, EditableContainers));
			}

			return SNullWidget::NullWidget;
		}
	);
}

TSharedRef<SWidget> CreateGameplayTagPropertyWidget(bool bReadOnly, UObject* TransactionObject, void* Value, UClass* ContextClass, UClass* ResultBaseClass)
{
	IHasContextClass* HasContextClass = Cast<IHasContextClass>(TransactionObject);

	FGameplayTagContextProperty* ContextProperty = reinterpret_cast<FGameplayTagContextProperty*>(Value);

	return SNew(SPropertyAccessChainWidget).ContextClassOwner(HasContextClass).AllowFunctions(false).BindingColor("StructPinTypeColor").TypeFilter("FGameplayTagContainer")
	.PropertyBindingValue(&ContextProperty->Binding)
	.OnAddBinding_Lambda(
		[ContextProperty, TransactionObject](FName InPropertyName, const TArray<FBindingChainElement>& InBindingChain)
		{
			const FScopedTransaction Transaction(NSLOCTEXT("ContextPropertyWidget", "Change Property Binding", "Change Property Binding"));
			TransactionObject->Modify(true);
			ContextProperty->SetBinding(InBindingChain);	
		});
}
	
void RegisterGameplayTagWidgets()
{
	FObjectChooserWidgetFactories::RegisterWidgetCreator(FGameplayTagContextProperty::StaticStruct(), CreateGameplayTagPropertyWidget);
	FObjectChooserWidgetFactories::RegisterColumnWidgetCreator(FGameplayTagColumn::StaticStruct(), CreateGameplayTagColumnWidget);
}
	
}

#undef LOCTEXT_NAMESPACE
