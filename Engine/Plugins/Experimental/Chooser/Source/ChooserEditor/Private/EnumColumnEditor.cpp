// Copyright Epic Games, Inc. All Rights Reserved.

#include "EnumColumnEditor.h"
#include "EnumColumn.h"
#include "ContextPropertyWidget.h"
#include "ObjectChooserWidgetFactories.h"
#include "ChooserTableEditor.h"
#include "GraphEditorSettings.h"
#include "SEnumCombo.h"
#include "TransactionCommon.h"
#include "Widgets/Input/SButton.h"

#define LOCTEXT_NAMESPACE "EnumColumnEditor"

namespace UE::ChooserEditor
{
	
// Wrapper widget for EnumComboBox which will reconstruct the combo box when the Enum has changed
class SEnumCell : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEnumCell)
		: _RowIndex(-1)
	{}

	SLATE_ARGUMENT(UObject*, TransactionObject)
	SLATE_ARGUMENT(FEnumColumn*, EnumColumn)
	SLATE_ATTRIBUTE(int, RowIndex);
            
	SLATE_END_ARGS()

	TSharedRef<SWidget> CreateEnumComboBox()
	{
		if (const FEnumColumn* EnumColumnPointer = EnumColumn)
		{
			if (EnumColumnPointer->InputValue.IsValid())
			{
				if (const UEnum* Enum = EnumColumnPointer->InputValue.Get<FChooserParameterEnumBase>().GetEnum())
				{
					return SNew(SEnumComboBox, Enum)
						.CurrentValue_Lambda([this]()
						{
							int Row = RowIndex.Get();
							return EnumColumn->RowValues.IsValidIndex(Row) ? static_cast<int32>(EnumColumn->RowValues[Row].Value) : 0;
						})
						.OnEnumSelectionChanged_Lambda([this](int32 EnumValue, ESelectInfo::Type)
						{
							int Row = RowIndex.Get();
							if (EnumColumn->RowValues.IsValidIndex(Row))
							{
								const FScopedTransaction Transaction(LOCTEXT("Edit RHS", "Edit Enum Value"));
								TransactionObject->Modify(true);
								EnumColumn->RowValues[Row].Value = static_cast<uint8>(EnumValue);
							}
						});
				}
			}
		}
		
		return SNullWidget::NullWidget;
	}

	void UpdateEnumComboBox()
	{
		ChildSlot[ CreateEnumComboBox()	];
	}

	void Construct( const FArguments& InArgs)
	{
		RowIndex = InArgs._RowIndex;
		EnumColumn = InArgs._EnumColumn;
		TransactionObject = InArgs._TransactionObject;

		if (const FEnumColumn* EnumColumnPointer = EnumColumn)
		{
			if (EnumColumnPointer->InputValue.IsValid())
			{
				EnumSource = EnumColumnPointer->InputValue.Get<FChooserParameterEnumBase>().GetEnum();
			}
		}

		UpdateEnumComboBox();

		int Row = RowIndex.Get();

		ChildSlot
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().MaxWidth(45.0f)
			[
				SNew(SButton).ButtonStyle(FAppStyle::Get(),"FlatButton").TextStyle(FAppStyle::Get(),"RichTextBlock.Bold").HAlign(HAlign_Center)
				.Text_Lambda([this, Row]()
				{
					// this is a bit of an odd thing to do, but as we are polling for the "=" / "!=" button state anyway,
					// also poll to see if the currently bound Enum has changed, and if it has, recreate the enum combo box.
					const UEnum* CurrentEnumSource = nullptr;
					if (EnumColumn->InputValue.IsValid())
					{
						CurrentEnumSource = EnumColumn->InputValue.Get<FChooserParameterEnumBase>().GetEnum(); 
					}
					if (EnumSource != CurrentEnumSource)
					{
						EnumComboBorder->SetContent(CreateEnumComboBox());
						EnumSource = EnumColumn->InputValue.Get<FChooserParameterEnumBase>().GetEnum();
					}
					
					return (EnumColumn->RowValues.IsValidIndex(Row) && EnumColumn->RowValues[Row].CompareNotEqual ? LOCTEXT("Not Equal", "!=") : LOCTEXT("Equal", "="));
				})
				.OnClicked_Lambda([this, Row]()
				{
					if (EnumColumn->RowValues.IsValidIndex(Row))
					{
						const FScopedTransaction Transaction(LOCTEXT("Edit Comparison", "Edit Comparison Operation"));
						TransactionObject->Modify(true);
						EnumColumn->RowValues[Row].CompareNotEqual = !EnumColumn->RowValues[Row].CompareNotEqual;
					}
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().FillWidth(true)
			[
				SAssignNew(EnumComboBorder, SBorder).Padding(0).BorderBackgroundColor(FLinearColor(0,0,0,0))
				[
					CreateEnumComboBox()
				]
			]
		];
		
	}

	~SEnumCell()
	{
	}

private:
	UObject* TransactionObject = nullptr;
	FEnumColumn* EnumColumn = nullptr;
	const UEnum* EnumSource = nullptr;
	TSharedPtr<SBorder> EnumComboBorder;
	TAttribute<int> RowIndex;
	FDelegateHandle EnumChangedHandle;
};

TSharedRef<SWidget> CreateEnumColumnWidget(UChooserTable* Chooser, FChooserColumnBase* Column, int Row)
{
	FEnumColumn* EnumColumn = static_cast<FEnumColumn*>(Column);
	
	return SNew(SEnumCell).TransactionObject(Chooser).EnumColumn(EnumColumn).RowIndex(Row);
}

TSharedRef<SWidget> CreateEnumPropertyWidget(bool bReadOnly, UObject* TransactionObject, void* Value, UClass* ContextClass, UClass* ResultBaseClass)
{
	return CreatePropertyWidget<FEnumContextProperty>(bReadOnly, TransactionObject, Value, ContextClass, GetDefault<UGraphEditorSettings>()->BytePinTypeColor);
}
	
void RegisterEnumWidgets()
{
	FObjectChooserWidgetFactories::RegisterWidgetCreator(FEnumContextProperty::StaticStruct(), CreateEnumPropertyWidget);
	FObjectChooserWidgetFactories::RegisterColumnWidgetCreator(FEnumColumn::StaticStruct(), CreateEnumColumnWidget);
}
	
}

#undef LOCTEXT_NAMESPACE
