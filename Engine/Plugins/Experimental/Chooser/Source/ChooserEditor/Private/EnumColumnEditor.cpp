// Copyright Epic Games, Inc. All Rights Reserved.

#include "EnumColumnEditor.h"
#include "EnumColumn.h"
#include "SPropertyAccessChainWidget.h"
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
						.IsEnabled_Lambda([this]
						{
							if (UChooserTable* Chooser = Cast<UChooserTable>(TransactionObject))
							{
								// return false only for the column header (RowIndex = -1) and when there is a debug target object bound
								return RowIndex.Get()>=0 || !Chooser->HasDebugTarget();
							}
							return true;
						})
						.CurrentValue_Lambda([this]()
						{
							int Row = RowIndex.Get();
							if (EnumColumn->RowValues.IsValidIndex(Row))
							{
								return EnumColumn->RowValues.IsValidIndex(Row) ? static_cast<int32>(EnumColumn->RowValues[Row].Value) : 0;
							}
							else
							{
								return EnumColumn->TestValue;
							}
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
							else
							{
								EnumColumn->TestValue = EnumValue;
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

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		const UEnum* CurrentEnumSource = nullptr;
		if (EnumColumn->InputValue.IsValid())
		{
			CurrentEnumSource = EnumColumn->InputValue.Get<FChooserParameterEnumBase>().GetEnum(); 
		}
		if (EnumSource != CurrentEnumSource)
		{
			EnumComboBorder->SetContent(CreateEnumComboBox());
			EnumSource = CurrentEnumSource;
		}
	}
    					

	void Construct( const FArguments& InArgs)
	{
		SetCanTick(true);
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
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(Row < 0 ? 0 : 45)
				[
					SNew(SButton).ButtonStyle(FAppStyle::Get(),"FlatButton").TextStyle(FAppStyle::Get(),"RichTextBlock.Bold").HAlign(HAlign_Center)
					.Visibility(Row < 0 ? EVisibility::Hidden : EVisibility::Visible)
					.Text_Lambda([this, Row]()
					{

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
			]
			+ SHorizontalBox::Slot().FillWidth(1)
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
	
	if (Row < 0)
	{
		// create column header widget
		TSharedPtr<SWidget> InputValueWidget = nullptr;
		if (FChooserParameterBase* InputValue = Column->GetInputValue())
		{
			InputValueWidget = FObjectChooserWidgetFactories::CreateWidget(false, Chooser, InputValue, Column->GetInputType(), Chooser->ContextObjectType, Chooser->OutputObjectType);
		}
		
		const FSlateBrush* ColumnIcon = FCoreStyle::Get().GetBrush("Icons.Filter");
		
		TSharedRef<SWidget> ColumnHeaderWidget = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBorder)
				.BorderBackgroundColor(FLinearColor(0,0,0,0))
				.Content()
				[
					SNew(SImage).Image(ColumnIcon)
				]
			]
			+ SHorizontalBox::Slot()
			[
				InputValueWidget ? InputValueWidget.ToSharedRef() : SNullWidget::NullWidget
			];
	
		if (Chooser->bEnableDebugTesting)
		{
			ColumnHeaderWidget = SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			[
				ColumnHeaderWidget
			]
			+ SVerticalBox::Slot()
			[
				SNew(SEnumCell).TransactionObject(Chooser).EnumColumn(EnumColumn).RowIndex(Row)
			];
		}

		return ColumnHeaderWidget;
	}

	// create cell widget
	
	return SNew(SEnumCell).TransactionObject(Chooser).EnumColumn(EnumColumn).RowIndex(Row);
}

TSharedRef<SWidget> CreateEnumPropertyWidget(bool bReadOnly, UObject* TransactionObject, void* Value, UClass* ContextClass, UClass* ResultBaseClass)
{
	IHasContextClass* HasContextClass = Cast<IHasContextClass>(TransactionObject);

	FEnumContextProperty* ContextProperty = reinterpret_cast<FEnumContextProperty*>(Value);

	return SNew(SPropertyAccessChainWidget).ContextClassOwner(HasContextClass).AllowFunctions(false).BindingColor("BytePinTypeColor").TypeFilter("enum")
	.PropertyBindingValue(&ContextProperty->Binding)
	.OnAddBinding_Lambda(
		[ContextProperty, TransactionObject](FName InPropertyName, const TArray<FBindingChainElement>& InBindingChain)
		{
			const FScopedTransaction Transaction(NSLOCTEXT("ContextPropertyWidget", "Change Property Binding", "Change Property Binding"));
			TransactionObject->Modify(true);
			ContextProperty->SetBinding(InBindingChain);	
		});
}
	
void RegisterEnumWidgets()
{
	FObjectChooserWidgetFactories::RegisterWidgetCreator(FEnumContextProperty::StaticStruct(), CreateEnumPropertyWidget);
	FObjectChooserWidgetFactories::RegisterColumnWidgetCreator(FEnumColumn::StaticStruct(), CreateEnumColumnWidget);
}
	
}

#undef LOCTEXT_NAMESPACE
