// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXFixtureTypeMatrixFunctionsEditorMatrixRow.h"

#include "DMXEditor.h"
#include "Library/DMXEntityFixtureType.h"
#include "Widgets/SNameListPicker.h"
#include "Widgets/FixtureType/DMXFixtureTypeMatrixFunctionsEditorItem.h"

#include "Widgets/Images/SImage.h"
#include "Widgets/Notifications/SPopUpErrorText.h"


#define LOCTEXT_NAMESPACE "SDMXFixtureTypeMatrixFunctionsEditorMatrixRow"

void SDMXFixtureTypeMatrixFunctionsEditorMatrixRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable, TSharedRef<FDMXFixtureTypeMatrixFunctionsEditorItem> InCellAttributeItem)
{
	CellAttributeItem = InCellAttributeItem;

	SMultiColumnTableRow<TSharedPtr<FDMXFixtureTypeMatrixFunctionsEditorItem>>::Construct(
		FSuperRowType::FArguments(),
		OwnerTable
	);
}

TSharedRef<SWidget> SDMXFixtureTypeMatrixFunctionsEditorMatrixRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	if (ColumnName == "Status")
	{
		return
			SNew(SBorder)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.BorderImage(FEditorStyle::GetBrush("NoBorder"))
			[
				SNew(SImage)
				.Image_Lambda([this]()
					{
						if (!CellAttributeItem->ErrorStatus.IsEmpty())
						{
							return FEditorStyle::GetBrush("Icons.Error");
						}

						if (!CellAttributeItem->WarningStatus.IsEmpty())
						{
							return FEditorStyle::GetBrush("Icons.Warning");
						}
						static const FSlateBrush EmptyBrush = FSlateNoResource();
						return &EmptyBrush;
					})
				.ToolTipText_Lambda([this]()
					{
						if (!CellAttributeItem->ErrorStatus.IsEmpty())
						{
							return CellAttributeItem->ErrorStatus;
						}
						else if (!CellAttributeItem->WarningStatus.IsEmpty())
						{
							return CellAttributeItem->WarningStatus;
						}
						return FText::GetEmpty();
					})
			];
	}
	else if (ColumnName == "Channel")
	{
		return
			SNew(SBorder)
			.Padding(4.f)
			.BorderImage(FEditorStyle::GetBrush("NoBorder"))
			[
				SNew(STextBlock)
				.Text(CellAttributeItem->ChannelNumberText)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
			];
	}
	else if (ColumnName == "Attribute")
	{
		return
			SNew(SNameListPicker)
			.OptionsSource(MakeAttributeLambda(&FDMXAttributeName::GetPossibleValues))
			.UpdateOptionsDelegate(&FDMXAttributeName::OnValuesChanged)
			.IsValid(this, &SDMXFixtureTypeMatrixFunctionsEditorMatrixRow::HasInvalidCellAttributeName)
			.Value(this, &SDMXFixtureTypeMatrixFunctionsEditorMatrixRow::GetCellAttributeName)
			.bCanBeNone(FDMXAttributeName::bCanBeNone)
			.bDisplayWarningIcon(true)
			.OnValueChanged(this, &SDMXFixtureTypeMatrixFunctionsEditorMatrixRow::SetCellAttributeName);
	}

	return SNullWidget::NullWidget;
}

bool SDMXFixtureTypeMatrixFunctionsEditorMatrixRow::HasInvalidCellAttributeName() const
{
	const FName CurrentValue = GetCellAttributeName();
	if (CurrentValue.IsEqual(FDMXNameListItem::None))
	{
		return true;
	}

	return FDMXAttributeName::IsValid(CurrentValue);
}

FName SDMXFixtureTypeMatrixFunctionsEditorMatrixRow::GetCellAttributeName() const
{
	return CellAttributeItem->GetCellAttributeName().GetName();
}

void SDMXFixtureTypeMatrixFunctionsEditorMatrixRow::SetCellAttributeName(FName NewValue)
{
	FDMXAttributeName NewAttributeName;
	NewAttributeName.SetFromName(NewValue);

	CellAttributeItem->SetCellAttributeName(NewAttributeName);
}

#undef LOCTEXT_NAMESPACE
