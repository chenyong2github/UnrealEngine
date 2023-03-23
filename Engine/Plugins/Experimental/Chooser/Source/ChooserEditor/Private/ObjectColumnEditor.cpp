// Copyright Epic Games, Inc. All Rights Reserved.

#include "ObjectColumnEditor.h"
#include "ContextPropertyWidget.h"
#include "GraphEditorSettings.h"
#include "ObjectChooserWidgetFactories.h"
#include "ObjectColumn.h"
#include "PropertyCustomizationHelpers.h"
#include "TransactionCommon.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "ObjectColumnEditor"

namespace UE::ChooserEditor
{
	static UClass* GetAllowedClass(const FObjectColumn* ObjectColumn)
	{
		UClass* AllowedClass = nullptr;
		if (const FChooserParameterObjectBase* InputValue = ObjectColumn->InputValue.GetPtr<FChooserParameterObjectBase>())
		{
			AllowedClass = InputValue->GetAllowedClass();
		}

		if (AllowedClass == nullptr)
		{
			AllowedClass = UObject::StaticClass();
		}

		return AllowedClass;
	}

	static TSharedRef<SWidget> CreateObjectPicker(UObject* TransactionObject, FObjectColumn* ObjectColumn, int32 Row)
	{
		return SNew(SObjectPropertyEntryBox)
			.ObjectPath_Lambda([ObjectColumn, Row]() {
				return ObjectColumn->RowValues.IsValidIndex(Row) ? ObjectColumn->RowValues[Row].Value.ToString() : FString();
			})
			.AllowedClass(GetAllowedClass(ObjectColumn))
			.OnObjectChanged_Lambda([TransactionObject, ObjectColumn, Row](const FAssetData& AssetData) {
				if (ObjectColumn->RowValues.IsValidIndex(Row))
				{
					const FScopedTransaction Transaction(LOCTEXT("Edit RHS", "Edit Object Value"));
					TransactionObject->Modify(true);
					ObjectColumn->RowValues[Row].Value = AssetData.ToSoftObjectPath();
				}
			})
			.DisplayUseSelected(false)
			.DisplayBrowse(false)
			.DisplayThumbnail(false)
			.Visibility_Lambda([ObjectColumn, Row]() {
				return (ObjectColumn->RowValues.IsValidIndex(Row) &&
						ObjectColumn->RowValues[Row].Comparison == EObjectColumnCellValueComparison::MatchAny)
						   ? EVisibility::Collapsed
						   : EVisibility::Visible;
			});
	}

	static TSharedRef<SWidget> CreateObjectColumnWidget(UChooserTable* Chooser, FChooserColumnBase* Column, int32 Row)
	{
		// Extend `SHorizontalBox` a little bit so we can poll for changes & recreate the object picker if necessary.
		struct SHorizontalBoxEx : public SHorizontalBox
		{
			virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
			{
				SHorizontalBox::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

				UClass* CurrentAllowedClass = GetAllowedClass(ObjectColumn);
				if (AllowedClass != CurrentAllowedClass)
				{
					AllowedClass = CurrentAllowedClass;
					GetSlot(1)[ObjectPickerFactory()];
				}
			}

			FObjectColumn* ObjectColumn = nullptr;
			UClass* AllowedClass = nullptr;
			TFunction<TSharedRef<SWidget>()> ObjectPickerFactory;
		};

		FObjectColumn* ObjectColumn = static_cast<FObjectColumn*>(Column);

		// clang-format off
		TSharedRef<SHorizontalBoxEx> CellWidget = SNew(SHorizontalBoxEx)
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(55)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton")
					.TextStyle(FAppStyle::Get(), "RichTextBlock.Bold")
					.HAlign(HAlign_Center)
					.Text_Lambda([ObjectColumn, Row]() {
						if (ObjectColumn->RowValues.IsValidIndex(Row))
						{
							switch (ObjectColumn->RowValues[Row].Comparison)
							{
								case EObjectColumnCellValueComparison::MatchEqual:
									return LOCTEXT("CompEqual", "=");

								case EObjectColumnCellValueComparison::MatchNotEqual:
									return LOCTEXT("CompNotEqual", "!=");

								case EObjectColumnCellValueComparison::MatchAny:
									return LOCTEXT("CompAny", "Any");
							}
						}
						return FText::GetEmpty();
					})
					.OnClicked_Lambda([Chooser, ObjectColumn, Row]() {
						if (ObjectColumn->RowValues.IsValidIndex(Row))
						{
							const FScopedTransaction Transaction(LOCTEXT("Edit Comparison", "Edit Comparison Operation"));
							Chooser->Modify(true);
							// cycle through comparison options
							EObjectColumnCellValueComparison& Comparison = ObjectColumn->RowValues[Row].Comparison;
							const int32 NextComparison = (static_cast<int32>(Comparison) + 1) % static_cast<int32>(EObjectColumnCellValueComparison::Modulus);
							Comparison = static_cast<EObjectColumnCellValueComparison>(NextComparison);
						}
						return FReply::Handled();
					})
				]
			]
			+SHorizontalBox::Slot()
			.FillWidth(1)
			[
				CreateObjectPicker(Chooser, ObjectColumn, Row)
			];
		// clang-format on

		// Setup the things we need to check for changes to the allowed class.
		{
			CellWidget->SetCanTick(true);
			CellWidget->ObjectColumn = ObjectColumn;
			CellWidget->AllowedClass = GetAllowedClass(ObjectColumn);
			CellWidget->ObjectPickerFactory = [Chooser, ObjectColumn, Row]() { return CreateObjectPicker(Chooser, ObjectColumn, Row); };
		}

		return CellWidget;
	}

	static TSharedRef<SWidget> CreateObjectPropertyWidget(bool bReadOnly, UObject* TransactionObject, void* Value, UClass* ContextClass, UClass* ResultBaseClass)
	{
		return CreatePropertyWidget<FObjectContextProperty>(bReadOnly, TransactionObject, Value, ContextClass, GetDefault<UGraphEditorSettings>()->ObjectPinTypeColor);
	}

	void RegisterObjectWidgets()
	{
		FObjectChooserWidgetFactories::RegisterWidgetCreator(FObjectContextProperty::StaticStruct(), CreateObjectPropertyWidget);
		FObjectChooserWidgetFactories::RegisterColumnWidgetCreator(FObjectColumn::StaticStruct(), CreateObjectColumnWidget);
	}

} // namespace UE::ChooserEditor

#undef LOCTEXT_NAMESPACE
