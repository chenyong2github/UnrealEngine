// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeTableDetails.h"
#include "MuCOE/Nodes/CustomizableObjectNode.h"
#include "MuCOE/Nodes/CustomizableObjectNodeTable.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"

#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Animation/AnimInstance.h"
#include "GameplayTagContainer.h"

#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "CustomizableObjectDetails"

TSharedRef<IDetailCustomization> FCustomizableObjectNodeTableDetails::MakeInstance()
{
	return MakeShareable(new FCustomizableObjectNodeTableDetails);
}


void FCustomizableObjectNodeTableDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	Node = 0;

	const IDetailsView* DetailsView = DetailBuilder.GetDetailsView();
	if (DetailsView->GetSelectedObjects().Num())
	{
		Node = Cast<UCustomizableObjectNodeTable>(DetailsView->GetSelectedObjects()[0].Get());
	}

	if (Node)
	{
		IDetailCategoryBuilder& CustomizableObjectCategory = DetailBuilder.EditCategory("CustomizableObject");
		IDetailCategoryBuilder& UICategory = DetailBuilder.EditCategory("UI");
		IDetailCategoryBuilder& LayoutCategory = DetailBuilder.EditCategory("LayoutEditor");

		GenerateColumnComboBoxOptions();

		LayoutCategory.AddCustomRow(LOCTEXT("TestName", "Test Name"))
		[
			SNew(SVerticalBox)
			
			// Mesh Column selection widget
			+SVerticalBox::Slot()
			.Padding(0.0f, 5.0f, 6.0f, 0.0f)
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 5.0f, 6.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ColumnText", "Column: "))
				]

				+ SHorizontalBox::Slot()
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("NoBorder"))
					.Padding(FMargin(0.0f, 0.0f, 10.0f, 0.0f))
					[
						SAssignNew(ColumnComboBox, STextComboBox)
						.OptionsSource(&ColumnOptionNames)
						.InitiallySelectedItem(nullptr)
						.OnSelectionChanged(this, &FCustomizableObjectNodeTableDetails::OnColumnComboBoxSelectionChanged)
					]
				]
			]
			
			// Layout selection widget
			+SVerticalBox::Slot()
			.Padding(0.0f, 5.0f, 6.0f, 0.0f)
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 5.0f, 6.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("LayoutText", "Layout: "))
				]

				+ SHorizontalBox::Slot()
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("NoBorder"))
					.Padding(FMargin(0.0f, 0.0f, 10.0f, 0.0f))
					[
						SAssignNew(LayoutComboBox, STextComboBox)
						.OptionsSource(&LayoutOptionNames)
						.InitiallySelectedItem(nullptr)
						.OnSelectionChanged(this, &FCustomizableObjectNodeTableDetails::OnLayoutComboBoxSelectionChanged)
					]
				]
			]

			// Animation Blueprint selection widget
			+SVerticalBox::Slot()
			.Padding(0.0f, 5.0f, 6.0f, 0.0f)
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 5.0f, 6.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AnimBPText", "Animation Column: "))
					.Visibility_Lambda([this]() -> EVisibility
					{
						if (!AnimComboBox.IsValid())
						{
							return EVisibility::Collapsed;
						}

						return AnimComboBox->GetVisibility();
					})
				]

				+ SHorizontalBox::Slot()
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("NoBorder"))
					.Padding(FMargin(0.0f, 0.0f, 10.0f, 0.0f))
					[
						SAssignNew(AnimComboBox, STextComboBox)
						.Visibility(EVisibility::Collapsed)
						.OptionsSource(&AnimOptionNames)
						.OnSelectionChanged(this, &FCustomizableObjectNodeTableDetails::OnAnimInstanceComboBoxSelectionChanged)
					]
				]
			]

			// Animation Slot selection widget
			+SVerticalBox::Slot()
			.Padding(0.0f, 5.0f, 6.0f, 0.0f)
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 5.0f, 6.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AnimSlotText", "Animation Slot Column: "))
					.Visibility_Lambda([this]() -> EVisibility 
						{
							if (!AnimSlotComboBox.IsValid())
							{
								return EVisibility::Collapsed;
							}

							return AnimSlotComboBox->GetVisibility();
						})
				]

				+ SHorizontalBox::Slot()
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("NoBorder"))
					.Padding(FMargin(0.0f, 0.0f, 10.0f, 0.0f))
					[
						SAssignNew(AnimSlotComboBox, STextComboBox)
						.Visibility(EVisibility::Collapsed)
						.OptionsSource(&AnimSlotOptionNames)
						.OnSelectionChanged(this, &FCustomizableObjectNodeTableDetails::OnAnimSlotComboBoxSelectionChanged)
					]
				]
			]

			// Animation Tags selection widget
			+SVerticalBox::Slot()
			.Padding(0.0f, 5.0f, 6.0f, 0.0f)
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 5.0f, 6.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AnimTagsText", "Animation Tags Column: "))
					.Visibility_Lambda([this]() -> EVisibility 
						{
							if (!AnimTagsComboBox.IsValid())
							{
								return EVisibility::Collapsed;
							}

							return AnimTagsComboBox->GetVisibility();
						})
				]

				+ SHorizontalBox::Slot()
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("NoBorder"))
					.Padding(FMargin(0.0f, 0.0f, 10.0f, 0.0f))
					[
						SAssignNew(AnimTagsComboBox, STextComboBox)
						.Visibility(EVisibility::Collapsed)
						.OptionsSource(&AnimTagsOptionNames)
						.OnSelectionChanged(this, &FCustomizableObjectNodeTableDetails::OnAnimTagsComboBoxSelectionChanged)
					]
				]
			]
		];
	}

}


void FCustomizableObjectNodeTableDetails::GenerateColumnComboBoxOptions()
{
	ColumnOptionNames.Empty();

	if (Node->Table)
	{
		const UScriptStruct* TableStruct = Node->Table->GetRowStruct();

		// we just need the mesh columns
		for (TFieldIterator<FProperty> It(TableStruct); It; ++It)
		{
			FProperty* ColumnProperty = *It;

			if (ColumnProperty)
			{
				if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(ColumnProperty))
				{
					if (SoftObjectProperty->PropertyClass->IsChildOf(USkeletalMesh::StaticClass())
						|| SoftObjectProperty->PropertyClass->IsChildOf(UStaticMesh::StaticClass()))
					{
						ColumnOptionNames.Add(MakeShareable(new FString(DataTableUtils::GetPropertyExportName(ColumnProperty))));
					}
				}
			}
		}
	}
}


void FCustomizableObjectNodeTableDetails::GenerateLayoutComboBoxOptions()
{
	LayoutOptionNames.Empty();
	
	for (const UEdGraphPin* Pin : Node->Pins)
	{
		const UCustomizableObjectNodeTableMeshPinData* PinData = Cast<UCustomizableObjectNodeTableMeshPinData >(Node->GetPinData(*Pin));

		if (PinData && PinData->ColumnName == *ColumnComboBox->GetSelectedItem().Get())
		{
			if (PinData->Layouts.Num() > 1)
			{
				for (int32 LayoutIndex = 0; LayoutIndex < PinData->Layouts.Num(); ++LayoutIndex)
				{
					FString LayoutName = Pin->PinFriendlyName.ToString() + FString::Printf(TEXT(" UV_%d"), LayoutIndex);
					LayoutOptionNames.Add(MakeShareable(new FString(LayoutName)));
				}
			}
			else
			{
				LayoutOptionNames.Add(MakeShareable(new FString(Pin->PinFriendlyName.ToString())));
			}
		}
	}
}


void FCustomizableObjectNodeTableDetails::GenerateAnimInstanceComboBoxOptions()
{
	AnimOptionNames.Empty();

	const UScriptStruct* TableStruct = Node->Table->GetRowStruct();

	for (TFieldIterator<FProperty> It(TableStruct); It; ++It)
	{
		if (FProperty* ColumnProperty = *It)
		{
			if (const FSoftClassProperty* SoftClassProperty = CastField<FSoftClassProperty>(ColumnProperty))
			{
				if (SoftClassProperty->MetaClass->IsChildOf(UAnimInstance::StaticClass()))
				{
					TSharedPtr<FString> Option = MakeShareable(new FString(DataTableUtils::GetPropertyExportName(ColumnProperty)));
					AnimOptionNames.Add(Option);

					for (const UEdGraphPin* Pin : Node->Pins)
					{
						const UCustomizableObjectNodeTableMeshPinData* PinData = Cast<UCustomizableObjectNodeTableMeshPinData >(Node->GetPinData(*Pin));

						if (PinData && PinData->ColumnName == *ColumnComboBox->GetSelectedItem().Get() && PinData->AnimInstanceColumnName == *Option)
						{
							AnimComboBox->SetSelectedItem(Option);
							break;
						}
					}
				}
			}
		}
	}
	
}


void FCustomizableObjectNodeTableDetails::GenerateAnimSlotComboBoxOptions()
{
	AnimSlotOptionNames.Empty();

	if (Node->Table)
	{
		const UScriptStruct* TableStruct = Node->Table->GetRowStruct();

		// we just need the mesh columns
		for (TFieldIterator<FProperty> It(TableStruct); It; ++It)
		{
			FProperty* ColumnProperty = *It;

			if (ColumnProperty)
			{
				if (const FIntProperty* NumProperty = CastField<FIntProperty>(ColumnProperty))
				{
					TSharedPtr<FString> Option = MakeShareable(new FString(DataTableUtils::GetPropertyExportName(ColumnProperty)));
					AnimSlotOptionNames.Add(Option);

					for (const UEdGraphPin* Pin : Node->Pins)
					{
						const UCustomizableObjectNodeTableMeshPinData* PinData = Cast<UCustomizableObjectNodeTableMeshPinData >(Node->GetPinData(*Pin));

						if (PinData && PinData->ColumnName == *ColumnComboBox->GetSelectedItem().Get() && PinData->AnimSlotColumnName == *Option)
						{
							AnimSlotComboBox->SetSelectedItem(Option);
							break;
						}
					}
				}
			}
		}
	}
}


void FCustomizableObjectNodeTableDetails::GenerateAnimTagsComboBoxOptions()
{
	AnimTagsOptionNames.Empty();

	if (Node->Table)
	{
		const UScriptStruct* TableStruct = Node->Table->GetRowStruct();

		// we just need the mesh columns
		for (TFieldIterator<FProperty> It(TableStruct); It; ++It)
		{
			FProperty* ColumnProperty = *It;

			if (ColumnProperty)
			{
				if (const FStructProperty* StructProperty = CastField<FStructProperty>(ColumnProperty))
				{
					if (StructProperty->Struct == TBaseStructure<FGameplayTagContainer>::Get())
					{
						TSharedPtr<FString> Option = MakeShareable(new FString(DataTableUtils::GetPropertyExportName(ColumnProperty)));
						AnimTagsOptionNames.Add(Option);

						for (const UEdGraphPin* Pin : Node->Pins)
						{
							const UCustomizableObjectNodeTableMeshPinData* PinData = Cast<UCustomizableObjectNodeTableMeshPinData >(Node->GetPinData(*Pin));

							if (PinData && PinData->ColumnName == *ColumnComboBox->GetSelectedItem().Get() && PinData->AnimTagColumnName == *Option)
							{
								AnimTagsComboBox->SetSelectedItem(Option);
								break;
							}
						}
					}
				}
			}
		}
	}
}


void FCustomizableObjectNodeTableDetails::OnColumnComboBoxSelectionChanged(TSharedPtr<FString> Selection, ESelectInfo::Type SelectInfo)
{
	if (Selection.IsValid())
	{
		if (LayoutComboBox.IsValid())
		{
			LayoutComboBox->ClearSelection();
			LayoutComboBox->RefreshOptions();

			GenerateLayoutComboBoxOptions();
		}

		if (AnimComboBox.IsValid() && AnimSlotComboBox.IsValid())
		{
			AnimComboBox->SetVisibility(EVisibility::Visible);
			AnimSlotComboBox->SetVisibility(EVisibility::Visible);
			AnimTagsComboBox->SetVisibility(EVisibility::Visible);

			GenerateAnimInstanceComboBoxOptions();
			GenerateAnimSlotComboBoxOptions();
			GenerateAnimTagsComboBoxOptions();
		}

		Node->SetLayoutInLayoutEditor(nullptr);
	}
}


void FCustomizableObjectNodeTableDetails::OnLayoutComboBoxSelectionChanged(TSharedPtr<FString> Selection, ESelectInfo::Type SelectInfo)
{
	if (Selection.IsValid())
	{
		FString ColumnName = *ColumnComboBox->GetSelectedItem();
		FString LayoutName = *Selection;

		for (const UEdGraphPin* Pin : Node->Pins)
		{
			const UCustomizableObjectNodeTableMeshPinData* PinData = Cast<UCustomizableObjectNodeTableMeshPinData >(Node->GetPinData(*Pin));
			
			if (PinData && PinData->ColumnName == ColumnName)
			{
				for (int32 LayoutIndex = 0; LayoutIndex < PinData->Layouts.Num(); ++LayoutIndex)
				{
					if (PinData->Layouts[LayoutIndex]->GetLayoutName() == LayoutName)
					{
						Node->SetLayoutInLayoutEditor(PinData->Layouts[LayoutIndex]);
					}
				}
			}
		}
	}
}


void FCustomizableObjectNodeTableDetails::OnAnimInstanceComboBoxSelectionChanged(TSharedPtr<FString> Selection, ESelectInfo::Type SelectInfo)
{
	if (Selection.IsValid() && ColumnComboBox->GetSelectedItem().IsValid())
	{
		FString ColumnName = *ColumnComboBox->GetSelectedItem();

		for (const UEdGraphPin* Pin : Node->Pins)
		{
			UCustomizableObjectNodeTableMeshPinData* PinData = Cast< UCustomizableObjectNodeTableMeshPinData>(Node->GetPinData(*Pin));

			if (PinData && PinData->ColumnName == ColumnName)
			{
				PinData->AnimInstanceColumnName = *Selection;
			}
		}

		Node->MarkPackageDirty();
	}
}


void FCustomizableObjectNodeTableDetails::OnAnimSlotComboBoxSelectionChanged(TSharedPtr<FString> Selection, ESelectInfo::Type SelectInfo)
{
	if (Selection.IsValid() && ColumnComboBox->GetSelectedItem().IsValid())
	{
		FString ColumnName = *ColumnComboBox->GetSelectedItem();

		for (const UEdGraphPin* Pin : Node->Pins)
		{
			UCustomizableObjectNodeTableMeshPinData* PinData = Cast< UCustomizableObjectNodeTableMeshPinData>(Node->GetPinData(*Pin));

			if (PinData && PinData->ColumnName == ColumnName)
			{
				PinData->AnimSlotColumnName = *Selection;
			}
		}

		Node->MarkPackageDirty();
	}
}


void FCustomizableObjectNodeTableDetails::OnAnimTagsComboBoxSelectionChanged(TSharedPtr<FString> Selection, ESelectInfo::Type SelectInfo)
{
	if (Selection.IsValid() && ColumnComboBox->GetSelectedItem().IsValid())
	{
		FString ColumnName = *ColumnComboBox->GetSelectedItem();

		for (const UEdGraphPin* Pin : Node->Pins)
		{
			UCustomizableObjectNodeTableMeshPinData* PinData = Cast< UCustomizableObjectNodeTableMeshPinData>(Node->GetPinData(*Pin));

			if (PinData && PinData->ColumnName == ColumnName)
			{
				PinData->AnimTagColumnName = *Selection;
			}
		}

		Node->MarkPackageDirty();
	}
}


#undef LOCTEXT_NAMESPACE
