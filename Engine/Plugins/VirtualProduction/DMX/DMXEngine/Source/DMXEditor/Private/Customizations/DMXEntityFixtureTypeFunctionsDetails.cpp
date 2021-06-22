// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/DMXEntityFixtureTypeFunctionsDetails.h"
#include "DMXEditor.h"
#include "DMXFixtureTypeSharedData.h"
#include "Customizations/Widgets/STypeFunctionsDetails.h"
#include "Library/DMXEntityFixtureType.h"
#include "Commands/DMXEditorCommands.h"

#include "DetailLayoutBuilder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IPropertyUtilities.h"

#define LOCTEXT_NAMESPACE "DMXEntityFixtureTypeFunctionsDetails"

void FDMXEntityFixtureTypeFunctionsDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	DetailBuilder.HideCategory("Entity Properties");
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, DMXImport));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, DMXCategory));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, bFixtureMatrixEnabled));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, Modes));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, InputModulators));

	if (TSharedPtr<FDMXEditor> DMXEditor = DMXEditorPtr.Pin())
	{
		// Add the 'Add New Function' Button, even if the functions list isn't shown.
		// Allows us to provide extra info to the UI user when functions can't be added
		// or aren't shown at all.
		DetailBuilder.EditCategory("Functions")
			.HeaderContent(
				AddHeaderContent()
			);

		PropertyUtilities = DetailBuilder.GetPropertyUtilities();
		check(PropertyUtilities.IsValid());

		SharedData = DMXEditor->GetFixtureTypeSharedData();
		check(SharedData.IsValid());

		TSharedPtr<IPropertyHandle> ModesHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, Modes));
		check(ModesHandle.IsValid() && ModesHandle->IsValidHandle());
		ModesHandleArray = ModesHandle->AsArray();
		check(ModesHandleArray.IsValid());

		FSimpleDelegate OnNumModesChangedDelegate = FSimpleDelegate::CreateSP(this, &FDMXEntityFixtureTypeFunctionsDetails::OnNumModesChanged);
		ModesHandleArray->SetOnNumElementsChanged(OnNumModesChangedDelegate);

		DetailBuilder.EditCategory("Functions")
			.AddCustomRow(LOCTEXT("Function", "Function"))
			.WholeRowContent()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				SNew(SBox)
				[
					SNew(SDMXFunctionItemListViewBox, DMXEditorPtr.Pin(), ModesHandleArray)
				]
			];
	}
}

void FDMXEntityFixtureTypeFunctionsDetails::OnNumModesChanged()
{
	// Invalidates Functions Arrays, needs refresh
	check(PropertyUtilities.IsValid());
	PropertyUtilities->ForceRefresh();
}

TSharedRef<SWidget> FDMXEntityFixtureTypeFunctionsDetails::AddHeaderContent()
{
	return
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(FMargin(5.f, 0.f))
			[
				SNew(STextBlock)
				.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
				.Text(LOCTEXT("Attribute", "Attribute"))
			]
			+SHorizontalBox::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.IsEnabled(this, &FDMXEntityFixtureTypeFunctionsDetails::GetIsAddFunctionButtonEnabled)
				.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
				.ForegroundColor(FLinearColor::White)
				.ToolTipText(this, &FDMXEntityFixtureTypeFunctionsDetails::GetAddFunctionButtonTooltipText)
				.ContentPadding(FMargin(5.0f, 1.0f))
				.OnClicked(this, &FDMXEntityFixtureTypeFunctionsDetails::OnAddFunctionButtonClicked)
				.Content()
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(FMargin(0, 1))
					[
						SNew(SImage)
						.Image(FEditorStyle::GetBrush("Plus"))
					]
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.AutoWidth()
					.Padding(FMargin(2, 0, 2, 0))
					[
						SNew(STextBlock)
						.Text(this, &FDMXEntityFixtureTypeFunctionsDetails::GetAddFunctionButtonText)
					]
				]
			]
		];
}

bool FDMXEntityFixtureTypeFunctionsDetails::GetIsAddFunctionButtonEnabled() const
{
	check(SharedData.IsValid());
	if (SharedData->CanAddFunction())
	{
		return true;
	}
	return false;
}

FText FDMXEntityFixtureTypeFunctionsDetails::GetAddFunctionButtonTooltipText() const
{	
	check(SharedData.IsValid());
	if (SharedData->CanAddFunction())
	{
		return FDMXEditorCommands::Get().AddNewModeFunction->GetDescription();
	}
	return LOCTEXT("AddFunctionButtonDisabledTooltip", "Please select a single Mode to add a Function to it.");
}

FReply FDMXEntityFixtureTypeFunctionsDetails::OnAddFunctionButtonClicked() const
{
	check(SharedData.IsValid());
	check(SharedData->CanAddFunction());

	SharedData->AddFunctionToSelectedMode();

	return FReply::Handled();
}

FText FDMXEntityFixtureTypeFunctionsDetails::GetAddFunctionButtonText() const
{
	check(SharedData.IsValid());
	if (SharedData->CanAddFunction())
	{
		return FDMXEditorCommands::Get().AddNewModeFunction->GetLabel();
	}
	return LOCTEXT("AddFunctionButtonDisabled", "Multiple Modes Selected");
}

#undef LOCTEXT_NAMESPACE
