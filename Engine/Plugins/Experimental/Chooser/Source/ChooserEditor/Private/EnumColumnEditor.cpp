// Copyright Epic Games, Inc. All Rights Reserved.

#include "EnumColumnEditor.h"
#include "EnumColumn.h"
#include "ContextPropertyWidget.h"
#include "ObjectChooserWidgetFactories.h"
#include "ChooserTableEditor.h"
#include "GraphEditorSettings.h"
#include "SEnumCombo.h"
#include "../../../../../Developer/TextureFormatOodle/Sdks/2.9.8/include/oodle2tex.h"

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

		SLATE_ARGUMENT(UChooserColumnEnum*, EnumColumn)
		SLATE_ATTRIBUTE(int, RowIndex);
                
		SLATE_END_ARGS()

		TSharedRef<SWidget> CreateEnumComboBox()
		{
			if (const UChooserColumnEnum* EnumColumnPointer = EnumColumn.Get())
			{
				if (EnumColumnPointer->InputValue)
				{
					if (const UEnum* Enum = EnumColumnPointer->InputValue->GetEnum())
					{
						return SNew(SEnumComboBox, Enum)
							.CurrentValue_Lambda([this]()
							{
								int Row = RowIndex.Get();
								return EnumColumn.Get()->RowValues.IsValidIndex(Row) ? static_cast<int32>(EnumColumn->RowValues[Row].Value) : 0;
							})
							.OnEnumSelectionChanged_Lambda([this](int32 EnumValue, ESelectInfo::Type)
							{
								int Row = RowIndex.Get();
								if (EnumColumn->RowValues.IsValidIndex(Row))
								{
									const FScopedTransaction Transaction(LOCTEXT("Edit RHS", "Edit Enum Value"));
									EnumColumn->Modify(true);
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

			if (UChooserColumnEnum* EnumColumnPointer = EnumColumn.Get())
			{
				EnumChangedHandle = EnumColumnPointer->OnEnumChanged.AddSP(this, &SEnumCell::UpdateEnumComboBox);
			}
			
			UpdateEnumComboBox();
		}

		~SEnumCell()
		{
			if (UChooserColumnEnum* EnumColumnPointer = EnumColumn.Get())
			{
				EnumColumnPointer->OnEnumChanged.Remove(EnumChangedHandle);
			}
		}

	private:
		TWeakObjectPtr<UChooserColumnEnum> EnumColumn;
		TAttribute<int> RowIndex;
		FDelegateHandle EnumChangedHandle;
	};

	TSharedRef<SWidget> CreateEnumColumnWidget(UObject* Column, int Row)
	{
		UChooserColumnEnum* EnumColumn = CastChecked<UChooserColumnEnum>(Column);
		
		return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().MaxWidth(95.0f)
		[
			SNew(SEnumComboBox, StaticEnum<EChooserEnumComparison>())
			.CurrentValue_Lambda([EnumColumn, Row]()
			{
				return EnumColumn->RowValues.IsValidIndex(Row) ? static_cast<int32>(EnumColumn->RowValues[Row].Comparison) : 0;
			})
			.OnEnumSelectionChanged_Lambda([EnumColumn, Row](int32 EnumValue, ESelectInfo::Type)
			{
				if (EnumColumn->RowValues.IsValidIndex(Row))
				{
					const FScopedTransaction Transaction(LOCTEXT("Edit Comparison", "Edit Comparison Operation"));
					EnumColumn->Modify(true);
					EnumColumn->RowValues[Row].Comparison = static_cast<EChooserEnumComparison>(EnumValue);
				}
			})
		]
		+ SHorizontalBox::Slot()
		[
			SNew(SEnumCell).EnumColumn(EnumColumn).RowIndex(Row)
		];
	}

	

TSharedRef<SWidget> CreateEnumPropertyWidget(UObject* Object, UClass* ContextClass)
{
	return CreatePropertyWidget<UChooserParameterEnum_ContextProperty>(Object, ContextClass, GetDefault<UGraphEditorSettings>()->BytePinTypeColor);
}
	
void RegisterEnumWidgets()
{
	FObjectChooserWidgetFactories::ChooserWidgetCreators.Add(UChooserParameterEnum_ContextProperty::StaticClass(), CreateEnumPropertyWidget);
	FObjectChooserWidgetFactories::ChooserTextConverter.Add(UChooserParameterEnum_ContextProperty::StaticClass(), ConvertToText_ContextProperty<UChooserParameterEnum_ContextProperty>);

	FChooserTableEditor::ColumnWidgetCreators.Add(UChooserColumnEnum::StaticClass(), CreateEnumColumnWidget);
}
	
}

#undef LOCTEXT_NAMESPACE
